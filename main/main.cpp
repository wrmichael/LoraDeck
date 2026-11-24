// LoRaDeck for the Heltec WiFi LoRa 32 V3: FT-LoRa QSOs (FT8-style messages in LoRa packets),
// logging, and a web control page. The OLED shows status; everything is operated from the web page.
#include "board.h"
#include "sx1262.h"
#include "oled.h"
#include "clock.h"
#include "conlog.h"
#include <cstring>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char* TAG = "main";
static App s_app;
volatile bool g_rebootReq = false;

static LoraConfig radioCfg() {
    LoraConfig c;
    c.freqHz = s_app.cfg.freqHz;
    c.sf = s_app.cfg.sf;
    c.bwKhz = s_app.cfg.bwKhz;
    c.cr = s_app.cfg.cr;
    c.powerDbm = s_app.cfg.powerDbm;
    return c;
}

static void forwardLog(const std::string& rec) {        // UDP ADIF to a logger (Log4OM etc.)
    if (s_app.cfg.logHost.empty()) return;
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char port[8];
    snprintf(port, sizeof port, "%d", s_app.cfg.logPort);
    if (getaddrinfo(s_app.cfg.logHost.c_str(), port, &hints, &res) != 0 || !res) return;
    int sk = socket(AF_INET, SOCK_DGRAM, 0);
    if (sk >= 0) {
        sendto(sk, rec.data(), rec.size(), 0, res->ai_addr, res->ai_addrlen);
        close(sk);
    }
    freeaddrinfo(res);
}

static void oledTask(void*) {
    char line[24];
    for (;;) {
        App::Glance g = s_app.glance();
        oled::clear();
        oled::bigText(0, 0, g.call.empty() ? "LoRaDeck" : g.call.c_str());
        time_t t = (time_t)(clk::utcMs() / 1000);
        struct tm tm;
        gmtime_r(&t, &tm);
        if (clk::utcValid()) snprintf(line, sizeof line, "%02d:%02d:%02dZ %s", tm.tm_hour, tm.tm_min, tm.tm_sec, g.tx ? "TX" : g.txEn ? "tx on" : "");
        else snprintf(line, sizeof line, "clock not set");
        oled::text(0, 2, line, g.tx);
        std::string ip = net_ip();
        oled::text(0, 3, ip.empty() ? net_ssid().c_str() : ip.c_str());
        snprintf(line, sizeof line, "QSO: %s", g.dx.empty() ? "-" : g.dx.c_str());
        oled::text(0, 4, line);
        oled::text(0, 5, g.last.substr(0, 21).c_str());
        auto st = sx1262::stats();
        snprintf(line, sizeof line, "T%u R%u E%u %ddBm", (unsigned)st.tx, (unsigned)st.rx, (unsigned)st.rxCrc, st.lastRssi);
        oled::text(0, 6, line);
        snprintf(line, sizeof line, "log %d", g.logged);
        oled::text(0, 7, line);
        oled::show();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

extern "C" void app_main(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    static wl_handle_t wl;
    esp_vfs_fat_mount_config_t mc = {};
    mc.format_if_mount_failed = true;
    mc.max_files = 6;
    mc.allocation_unit_size = 4096;
    if (esp_vfs_fat_spiflash_mount_rw_wl("/data", "storage", &mc, &wl) != ESP_OK) ESP_LOGE(TAG, "storage did not mount");
    else conlog_start();
    s_app.version = esp_app_get_description()->version;
    s_app.trace = [](const std::string& line) { ESP_LOGI("app", "%s", line.c_str()); };
    s_app.begin("/data");
    ESP_LOGI(TAG, "LoRaDeck %s, call=%s", s_app.version.c_str(), s_app.cfg.call.c_str());

    oled::begin();
    oled::clear();
    oled::bigText(0, 2, s_app.cfg.call.empty() ? "LoRaDeck" : s_app.cfg.call.c_str());
    oled::text(0, 5, "LoRaDeck by AC9HP");
    oled::text(0, 7, s_app.version.c_str());
    oled::show();

    sx1262::onReceive = [](const uint8_t* d, int n, int rssi, float snr) { s_app.onPacket(d, n, rssi, snr); };
    sx1262::onSent = [] { s_app.onTxDone(); };
    bool radioOk = sx1262::begin(radioCfg());
    s_app.radioSend = [radioOk](const uint8_t* d, int n) { return radioOk && sx1262::send(d, n); };
    s_app.radioConfigure = [] { sx1262::configure(radioCfg()); };
    s_app.onLogged = forwardLog;

    net_start(s_app);
    web_start(s_app);
    ota_start();
    vTaskDelay(pdMS_TO_TICKS(2500));                      // the start-up screen for a moment
    xTaskCreate(oledTask, "oled", 4096, nullptr, 2, nullptr);
    for (;;) {
        s_app.tick();
        if (g_rebootReq) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
