// Firmware update from the web page: the page sends the image in 32 kB pieces, each with a CRC-32;
// a damaged piece is sent again. The board keeps running its firmware until the whole image has
// passed ESP-IDF's checks, then switches and restarts. A new image that does not run for a minute is
// rolled back by the bootloader.
#include "board.h"
#include <cstring>
#include <cstdlib>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "ota";
static App* s_app;
static std::atomic<bool> s_busy{false};
static const size_t kPiece = 65536;
static const uint16_t kChipS3 = 9;                    // esp_chip_id_t ESP_CHIP_ID_ESP32S3

static struct {
    bool active = false;
    esp_ota_handle_t h = 0;
    const esp_partition_t* part = nullptr;
    size_t total = 0, written = 0;
    int64_t lastUs = 0;
    int bad = 0;
    char ver[33] = {};
} s;

static esp_err_t reply(httpd_req_t* req, bool ok, const std::string& msg, bool retry = false) {
    std::string js = std::string("{\"ok\":") + (ok ? "true" : "false") + (retry ? ",\"retry\":true" : "") +
                     (ok ? ",\"msg\":\"" : ",\"err\":\"") + msg + "\"}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, js.data(), js.size());
}
static bool qnum(httpd_req_t* req, const char* k, uint32_t& out) {
    char q[128], v[24];
    if (httpd_req_get_url_query_str(req, q, sizeof q) != ESP_OK || httpd_query_key_value(q, k, v, sizeof v) != ESP_OK) return false;
    out = (uint32_t)strtoul(v, nullptr, 0);
    return true;
}
static void abortSession() {
    if (s.active) esp_ota_abort(s.h);
    s.active = false;
    s.written = 0;
}
static void restartTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    g_rebootReq = true;
    vTaskDelete(nullptr);
}

static esp_err_t chunk(httpd_req_t* req) {
    uint32_t off = 0, total = 0, crc = 0;
    if (!qnum(req, "off", off) || !qnum(req, "total", total) || !qnum(req, "crc", crc)) return reply(req, false, "bad request");
    size_t n = req->content_len;
    if (!n || n > kPiece || off + n > total) return reply(req, false, "bad piece size");
    if (s_busy.exchange(true)) return reply(req, false, "busy");
    struct Done { ~Done() { s_busy = false; } } done;
    if (s.active && esp_timer_get_time() - s.lastUs > 60000000LL) abortSession();
    static uint8_t* piece = nullptr;
    if (!piece) piece = (uint8_t*)malloc(kPiece);
    if (!piece) return reply(req, false, "not enough memory");
    size_t got = 0;
    while (got < n) {
        int r = httpd_req_recv(req, (char*)piece + got, n - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return reply(req, false, "piece interrupted", true);
        got += r;
    }
    if (esp_rom_crc32_le(0, piece, n) != crc) { s.bad++; return reply(req, false, "damaged piece", true); }
    if (off == 0) {
        abortSession();
        if (piece[0] != ESP_IMAGE_HEADER_MAGIC || (piece[12] | piece[13] << 8) != kChipS3)
            return reply(req, false, "That is not LoRaDeck firmware for the ESP32-S3. Nothing was changed.");
        const esp_app_desc_t* d = (const esp_app_desc_t*)(piece + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
        if (d->magic_word != ESP_APP_DESC_MAGIC_WORD || strncmp(d->project_name, "loradeck", 32) != 0)
            return reply(req, false, "That is not LoRaDeck firmware. Nothing was changed.");
        memcpy(s.ver, d->version, 32);
        s.part = esp_ota_get_next_update_partition(nullptr);
        if (!s.part || total > s.part->size) return reply(req, false, "no room for that image");
        s_app->command("halt", {});
        if (esp_ota_begin(s.part, OTA_WITH_SEQUENTIAL_WRITES, &s.h) != ESP_OK) return reply(req, false, "could not start the update");
        s.active = true;
        s.total = total;
        s.written = 0;
        s.bad = 0;
        ESP_LOGI(TAG, "update to %s, %u bytes, into %s", s.ver, (unsigned)total, s.part->label);
    }
    if (!s.active || total != s.total) return reply(req, false, "No update in progress. Start again.");
    s.lastUs = esp_timer_get_time();
    if (off + n <= s.written) return reply(req, true, "already have it");
    if (off != s.written) return reply(req, false, "pieces out of order. Start again.");
    if (esp_ota_write(s.h, piece, n) != ESP_OK) { abortSession(); return reply(req, false, "writing failed; current firmware kept"); }
    s.written += n;
    if (s.written < s.total) return reply(req, true, "");
    esp_err_t e = esp_ota_end(s.h);
    s.active = false;
    if (e != ESP_OK) return reply(req, false, std::string("the image did not pass the check (") + esp_err_to_name(e) + "); current firmware kept");
    if (esp_ota_set_boot_partition(s.part) != ESP_OK) return reply(req, false, "could not switch to the new firmware");
    xTaskCreate(restartTask, "otarst", 3072, nullptr, 2, nullptr);
    return reply(req, true, std::string("LoRaDeck ") + s.ver + " installed. Restarting; reconnect in about 20 seconds.");
}

void ota_register(httpd_handle_t srv, App& app) {
    s_app = &app;
    static const httpd_uri_t u = {"/api/otachunk", HTTP_POST, chunk, nullptr};
    httpd_register_uri_handler(srv, &u);
}

void ota_start() {
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        esp_ota_img_states_t st;
        const esp_partition_t* run = esp_ota_get_running_partition();
        if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY)
            esp_ota_mark_app_valid_cancel_rollback();
        vTaskDelete(nullptr);
    }, "otaok", 3072, nullptr, 2, nullptr);
}
