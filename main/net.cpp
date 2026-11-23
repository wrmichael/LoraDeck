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

static const char* TAG = "net";
static App* s_app;
static esp_netif_t* s_sta;
static std::string s_ip, s_state = "off";
static int s_retry = 0;

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
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) { s_state = "joining"; esp_wifi_connect(); }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip.clear();
        s_state = "retrying";
        if (++s_retry < 1000) esp_wifi_connect();
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* e = (ip_event_got_ip_t*)data;
        char b[20];
        snprintf(b, sizeof b, IPSTR, IP2STR(&e->ip_info.ip));
        s_ip = b;
        s_state = "connected";
        s_retry = 0;
        ESP_LOGI(TAG, "home network connected, address %s", b);
    }
}

void net_start(App& app) {
    s_app = &app;
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    bool sta = !app.cfg.staSsid.empty();
    if (sta) {
        s_sta = esp_netif_create_default_wifi_sta();
        esp_netif_set_hostname(s_sta, hostName().c_str());
    }
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wc);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr);
    esp_wifi_set_mode(sta ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    wifi_config_t ap = {};
    strlcpy((char*)ap.ap.ssid, app.cfg.apSsid.c_str(), sizeof ap.ap.ssid);
    strlcpy((char*)ap.ap.password, app.cfg.apPass.c_str(), sizeof ap.ap.password);
    ap.ap.ssid_len = (uint8_t)strlen((char*)ap.ap.ssid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (sta) {
        wifi_config_t st = {};
        strlcpy((char*)st.sta.ssid, app.cfg.staSsid.c_str(), sizeof st.sta.ssid);
        strlcpy((char*)st.sta.password, app.cfg.staPass.c_str(), sizeof st.sta.password);
        st.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        st.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        esp_wifi_set_config(WIFI_IF_STA, &st);
    }
    esp_wifi_start();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb([](struct timeval*) { clk::setUtcMs(clk::utcMs()); ESP_LOGI(TAG, "clock set from NTP"); });
    esp_sntp_init();
    ESP_LOGI(TAG, "hotspot \"%s\" at 192.168.4.1%s", app.cfg.apSsid.c_str(), sta ? ", joining the home network" : "");
}

std::string net_status() {
    std::string s = "Hotspot " + s_app->cfg.apSsid + " (192.168.4.1)";
    if (!s_app->cfg.staSsid.empty())
        s += ", home network " + s_app->cfg.staSsid + ": " + (s_ip.empty() ? s_state : s_ip);
    return s;
}
std::string net_ip() { return s_ip; }
