// LoRaDeck for the Heltec WiFi LoRa 32 V3: FT-LoRa QSOs (FT8-style messages in LoRa packets),
// logging, and a web control page. The OLED shows status; everything is operated from the web page.
#include "board.h"
#include "sx1262.h"
#include "oled.h"
#include "clock.h"
#include "conlog.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <cstring>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "wear_levelling.h"
#include "driver/gpio.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char* TAG = "main";
static App s_app;
volatile bool g_rebootReq = false;
static volatile int s_resetReq = 0;                       // 1 settings, 2 everything (format the storage)
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static const char* s_storageLabel = "storage";           // the FAT partition in use (another under M5Launcher)

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

// Storage health: a volume whose tables were wiped (all 0xFF) reports no free space and lists junk names. Then
// the page shows a warning with what to do (download the log, Factory reset -> Everything).
volatile bool g_storageBad = false;
static long scanDir(const std::string& dir, int depth, int& bad) {
    long sum = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;
    int n = 0;
    while (struct dirent* e = readdir(d)) {
        if (++n > 400) { bad++; break; }                  // far more entries than LoRaDeck ever makes
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        for (unsigned char c : name) if (c < 0x20 || c >= 0x7F) { bad++; break; }
        std::string p = dir + "/" + name;
        struct stat st = {};
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 2) sum += scanDir(p, depth + 1, bad); }
        else sum += (long)st.st_size;
    }
    closedir(d);
    return sum;
}
static void storageCheck() {
    uint64_t total = 0, freeB = 0;
    if (esp_vfs_fat_info("/data", &total, &freeB) != ESP_OK || !total) return;
    int bad = 0;
    long files = scanDir("/data", 0, bad);
    long used = (long)(total - freeB);
    // used space far beyond the files (more than 1 MB unaccounted for), or junk names
    g_storageBad = bad > 0 || used - files > 1024L * 1024;
    if (g_storageBad) ESP_LOGE(TAG, "storage looks damaged: %ld KB used, %ld KB in files, %d bad names; "
                               "download the log, then Factory reset -> Everything", used / 1024, files / 1024, bad);
}

// Factory reset: settings only (the files are removed), or everything (the storage partition is erased; it is
// formatted when it does not mount at the next start). Then restart with the default hotspot.
static void doFactoryReset(int what) {
    oled::clear();
    oled::bigText(0, 0, "RESET");
    oled::text(0, 3, what == 2 ? "settings and logs" : "settings (log kept)");
    oled::text(0, 5, "restarting...");
    oled::show();
    ESP_LOGW(TAG, "factory reset: %s", what == 2 ? "everything" : "settings");
    vTaskDelay(pdMS_TO_TICKS(800));
    if (what == 2) {
        // Format through the file system and its wear levelling (a raw erase under wear levelling can leave a
        // half-wiped, damaged volume if anything interrupts it). Only if formatting fails: erase it all, which
        // makes the next start format it.
        esp_err_t e = s_storageLabel ? esp_vfs_fat_spiflash_format_rw_wl("/data", s_storageLabel) : ESP_FAIL;
        ESP_LOGW(TAG, "storage formatted: %s", esp_err_to_name(e));
        if (e != ESP_OK) {
            if (s_wl != WL_INVALID_HANDLE) esp_vfs_fat_spiflash_unmount_rw_wl("/data", s_wl);
            const esp_partition_t* p = s_storageLabel ? esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, s_storageLabel) : nullptr;
            if (p) esp_partition_erase_range(p, 0, p->size);
        }
    } else {
        remove("/data/settings.txt");
        remove("/data/settings.txt.new");
    }
    nvs_flash_erase();                                    // Wi-Fi driver data too
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void oledTask(void*) {
    char line[32];
    int prgHeld = 0;                                      // PRG button held this many half seconds
    gpio_config_t prg = {};                               // PRG button (GPIO 0, to ground): hold 10 s = reset
    prg.pin_bit_mask = 1ULL << 0;
    prg.mode = GPIO_MODE_INPUT;
    prg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&prg);
    for (;;) {
        if (gpio_get_level(GPIO_NUM_0) == 0) {
            prgHeld++;
            if (prgHeld >= 20) { s_resetReq = 1; vTaskDelay(portMAX_DELAY); }
            oled::clear();
            oled::bigText(0, 0, "RESET?");
            int left = (21 - prgHeld) / 2;
            snprintf(line, sizeof line, "keep holding PRG %d s", left < 0 ? 0 : left > 10 ? 10 : left);
            oled::text(0, 3, line);
            oled::text(0, 4, "for a settings reset");
            oled::text(0, 6, "let go to cancel");
            oled::show();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        prgHeld = 0;
        App::Glance g = s_app.glance();
        oled::clear();
        oled::bigText(0, 0, g.call.empty() ? "LoRaDeck" : g.call.c_str());
        time_t t = (time_t)(clk::utcMs() / 1000);
        struct tm tm;
        gmtime_r(&t, &tm);
        static const char srcCh[] = {' ', 'B', 'G', 'N', 'P'};    // browser, GPS, NTP, GPS+PPS
        if (clk::utcValid()) snprintf(line, sizeof line, "%02d:%02d:%02dZ%c %s", tm.tm_hour, tm.tm_min, tm.tm_sec,
                                      srcCh[(int)clk::source() % 5], g.tx ? "TX" : g.txEn ? "tx on" : "");
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
    esp_vfs_fat_mount_config_t mc = {};
    mc.format_if_mount_failed = true;
    mc.max_files = 6;
    mc.allocation_unit_size = 4096;
    // LoRaDeck's own partition table names it "storage"; started from an SD card by M5Launcher the table is
    // Launcher's, so take its first FAT data partition instead
    const char* label = "storage";
    s_storageLabel = label;
    if (!esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage")) {
        const esp_partition_t* fat = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, nullptr);
        label = fat ? fat->label : nullptr;
        s_storageLabel = label;
        ESP_LOGW(TAG, "no \"storage\" partition (not LoRaDeck's partition table): using %s", label ? label : "none");
    }
    if (!label) ESP_LOGE(TAG, "no FAT partition: settings and log cannot be saved");
    else if (esp_vfs_fat_spiflash_mount_rw_wl("/data", label, &mc, &s_wl) != ESP_OK) ESP_LOGE(TAG, "storage did not mount");
    else {
        storageCheck();
        conlog_start();
    }
    s_app.version = esp_app_get_description()->version;
    s_app.trace = [](const std::string& line) { ESP_LOGI("app", "%s", line.c_str()); };
    s_app.begin("/data");
    ESP_LOGI(TAG, "LoRaDeck %s, call=%s", s_app.version.c_str(), s_app.cfg.call.c_str());

    bool boardOk = board_init();                          // Cardputer: the Cap LoRa-1262's antenna switch
    oled::begin();
    oled::clear();
    oled::bigText(0, 2, s_app.cfg.call.empty() ? "LoRaDeck" : s_app.cfg.call.c_str());
    oled::text(0, 5, "LoRaDeck by AC9HP");
    oled::text(0, 7, boardOk ? s_app.version.c_str() : "Cap LoRa not found!");
    oled::show();
    ESP_LOGI(TAG, "board: %s", board_name());

    sx1262::onReceive = [](const uint8_t* d, int n, int rssi, float snr) { s_app.onPacket(d, n, rssi, snr); };
    sx1262::onSent = [] { s_app.onTxDone(); };
    bool radioOk = sx1262::begin(radioCfg());
    s_app.airMs = sx1262::airtimeMs(radioCfg(), FTL_PACKET);
    s_app.radioSend = [radioOk](const uint8_t* d, int n) { return radioOk && sx1262::send(d, n); };
    s_app.radioCw = [radioOk](const std::string& t, int wpm) { return radioOk && sx1262::sendCw(t, wpm); };
    s_app.radioAbort = [] { sx1262::abortTx(); };
    s_app.airtime = [](int len) { return sx1262::airtimeMs(radioCfg(), len); };
    s_app.radioConfigure = [] {
        sx1262::configure(radioCfg());
        s_app.airMs = sx1262::airtimeMs(radioCfg(), FTL_PACKET);
    };
    s_app.onLogged = forwardLog;
    s_app.factoryReset = [](bool all) { s_resetReq = all ? 2 : 1; };

    net_start(s_app);
    gps_start(s_app);
    web_start(s_app);
    ota_start();
    vTaskDelay(pdMS_TO_TICKS(2500));                      // the start-up screen for a moment
    xTaskCreate(oledTask, "oled", 4096, nullptr, 2, nullptr);
    for (;;) {
        s_app.tick();
        if (g_rebootReq) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
        if (s_resetReq) { vTaskDelay(pdMS_TO_TICKS(500)); doFactoryReset(s_resetReq); }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
