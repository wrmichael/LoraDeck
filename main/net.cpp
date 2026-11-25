// Wi-Fi: the LoRaDeck hotspot (always on) and, if set, the home network; internet time (SNTP).
#include "board.h"
#include <cstring>
#include <cctype>
#include <string>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sntp.h"
#include "clock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "net";
static App* s_app;
static esp_netif_t* s_sta;
static std::string s_ip, s_state = "off";
static int s_retry = 0;
static std::string s_ssid;
static volatile bool s_scanning = false;             // no reconnecting while a scan runs

static std::string hostName() {                // DNS-safe: the hotspot name, or LoRaDeck-XXXXXX
    std::string n;
    for (char ch : s_app->cfg.apSsid) n += (isalnum((unsigned char)ch) || ch == '-') ? ch : '-';
    while (!n.empty() && n.front() == '-') n.erase(0, 1);
    while (!n.empty() && n.back() == '-') n.pop_back();
    if (n.size() > 32) n.resize(32);
    if (n.empty() || n == "LoRaDeck") {
        uint8_t m[6] = {};
        esp_read_mac(m, ESP_MAC_WIFI_STA);
        char b[24];
        snprintf(b, sizeof b, "LoRaDeck-%02X%02X%02X", m[3], m[4], m[5]);
        n = b;
    }
    return n;
}

static void onEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    bool home = !s_app->cfg.staSsid.empty();                // the station side is also started for a scan
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && home) { s_state = "joining"; esp_wifi_connect(); }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip.clear();
        s_state = home ? "retrying" : "off";
        if (home && !s_scanning && ++s_retry < 1000) esp_wifi_connect();
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* e = (ip_event_got_ip_t*)data;
        char b[20];
        snprintf(b, sizeof b, IPSTR, IP2STR(&e->ip_info.ip));
        s_ip = b;
        s_state = "connected";
        s_retry = 0;
        ESP_LOGI(TAG, "home network connected, address %s; asking NTP for the time", b);
        sntp_restart();                         // internet time right away, not at the next poll
    }
}

static void apConfig() {
    wifi_config_t ap = {};
    // The default name gets the board's MAC suffix, so several LoRaDecks in one room are told apart.
    std::string ssid = s_app->cfg.apSsid == "LoRaDeck" ? hostName() : s_app->cfg.apSsid;
    s_ssid = ssid;
    strlcpy((char*)ap.ap.ssid, ssid.c_str(), sizeof ap.ap.ssid);
    strlcpy((char*)ap.ap.password, s_app->cfg.apPass.c_str(), sizeof ap.ap.password);
    ap.ap.ssid_len = (uint8_t)strlen((char*)ap.ap.ssid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_AP, &ap);
}

static void staConfig() {
    wifi_config_t st = {};
    strlcpy((char*)st.sta.ssid, s_app->cfg.staSsid.c_str(), sizeof st.sta.ssid);
    strlcpy((char*)st.sta.password, s_app->cfg.staPass.c_str(), sizeof st.sta.password);
    st.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    st.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_config(WIFI_IF_STA, &st);
}

void net_start(App& app) {
    s_app = &app;
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    bool sta = !app.cfg.staSsid.empty();
    s_sta = esp_netif_create_default_wifi_sta();          // always: a home network can be joined later
    esp_netif_set_hostname(s_sta, hostName().c_str());
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wc);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr);
    esp_wifi_set_mode(sta ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    apConfig();
    if (sta) staConfig();
    esp_wifi_start();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_interval(15 * 60 * 1000);   // every 15 minutes: the board's clock drifts a little
    // SNTP has already set the clock when this is called. A GPS with PPS set within 30 minutes stays in charge:
    // the clock is then only marked, and the GPS corrects it again within a second.
    sntp_set_time_sync_notification_cb([](struct timeval*) {
        clk::markSet(clk::mayOverride(clk::SRC_NTP) ? clk::SRC_NTP : clk::source());
        ESP_LOGI(TAG, "clock set from NTP");
    });
    esp_sntp_init();
    ESP_LOGI(TAG, "hotspot \"%s\" at 192.168.4.1%s", s_ssid.c_str(), sta ? ", joining the home network" : "");
}

std::string net_status() {
    std::string s = "Hotspot " + s_ssid + " (192.168.4.1)";
    if (!s_app->cfg.staSsid.empty())
        s += ", home network " + s_app->cfg.staSsid + ": " + (s_ip.empty() ? s_state : s_ip);
    return s;
}
std::string net_ip() { return s_ip; }
std::string net_ssid() { return s_ssid; }

std::string net_state() { return s_app->cfg.staSsid.empty() ? std::string("not set") : s_ip.empty() ? s_state : s_ip; }

// Networks in reach, strongest first (blocking, a few seconds). The station side is switched on for it.
int net_scan(std::vector<WifiNet>& out) {
    out.clear();
    wifi_mode_t m = WIFI_MODE_AP;
    esp_wifi_get_mode(&m);
    if (m == WIFI_MODE_AP) esp_wifi_set_mode(WIFI_MODE_APSTA);
    // a station still trying to join cannot scan: pause it for the scan
    bool paused = !s_app->cfg.staSsid.empty() && s_ip.empty();
    if (paused) { s_scanning = true; esp_wifi_disconnect(); vTaskDelay(pdMS_TO_TICKS(200)); }
    wifi_scan_config_t sc = {};
    esp_err_t e = esp_wifi_scan_start(&sc, true);
    if (e == ESP_OK) {
        static wifi_ap_record_t rec[24];
        uint16_t n = 24;
        esp_wifi_scan_get_ap_records(&n, rec);
        for (int i = 0; i < n; i++) {
            std::string name((const char*)rec[i].ssid);
            if (name.empty()) continue;
            bool dup = false;
            for (const WifiNet& w : out) if (w.ssid == name) dup = true;
            if (!dup) out.push_back({name, rec[i].rssi, rec[i].authmode == WIFI_AUTH_OPEN});
        }
    } else ESP_LOGW(TAG, "Wi-Fi scan: %s", esp_err_to_name(e));
    if (paused) { s_scanning = false; esp_wifi_connect(); }
    if (m == WIFI_MODE_AP && s_app->cfg.staSsid.empty()) esp_wifi_set_mode(WIFI_MODE_AP);
    return e == ESP_OK ? (int)out.size() : -1;
}

// The home network in the settings, now (empty: leave it). The hotspot stays on.
void net_join() {
    s_retry = 0;
    s_ip.clear();
    esp_wifi_disconnect();
    if (s_app->cfg.staSsid.empty()) {
        s_state = "off";
        esp_wifi_set_mode(WIFI_MODE_AP);
        ESP_LOGI(TAG, "home network left");
        return;
    }
    wifi_mode_t m = WIFI_MODE_AP;
    esp_wifi_get_mode(&m);
    esp_netif_set_hostname(s_sta, hostName().c_str());
    s_state = "joining";
    if (m == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);               // STA_START follows and connects
        staConfig();
    } else {
        staConfig();
        esp_wifi_connect();
    }
    ESP_LOGI(TAG, "joining the home network %s", s_app->cfg.staSsid.c_str());
}

// The hotspot name and password in the settings, now (phones on it are disconnected and join again).
void net_hotspot() {
    apConfig();
    ESP_LOGI(TAG, "hotspot now \"%s\"", s_ssid.c_str());
}
