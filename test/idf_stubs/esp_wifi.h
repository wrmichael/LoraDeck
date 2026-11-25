#pragma once
// stub: only what net.cpp uses (not ESP-IDF)
#include <cstdint>
#include "driver/uart.h"
#include "esp_event.h"
typedef enum { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_IF_STA, WIFI_IF_AP } wifi_interface_t;
typedef enum { WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK } wifi_auth_mode_t;
enum { WIFI_ALL_CHANNEL_SCAN = 1, WIFI_CONNECT_AP_BY_SIGNAL = 0 };
enum { WIFI_EVENT_STA_START = 2, WIFI_EVENT_STA_DISCONNECTED = 5 };
struct wifi_ap_config_t { uint8_t ssid[32]; uint8_t password[64]; uint8_t ssid_len; uint8_t channel; wifi_auth_mode_t authmode; uint8_t max_connection; };
struct wifi_sta_config_t { uint8_t ssid[32]; uint8_t password[64]; int scan_method; int sort_method; };
typedef union { wifi_ap_config_t ap; wifi_sta_config_t sta; } wifi_config_t;
struct wifi_init_config_t { int x; };
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{0}
struct wifi_scan_config_t { const uint8_t* ssid; uint8_t channel; bool show_hidden; };
struct wifi_ap_record_t { uint8_t bssid[6]; uint8_t ssid[33]; uint8_t primary; int8_t rssi; wifi_auth_mode_t authmode; };
esp_err_t esp_wifi_init(const wifi_init_config_t*); esp_err_t esp_wifi_set_mode(wifi_mode_t); esp_err_t esp_wifi_get_mode(wifi_mode_t*);
esp_err_t esp_wifi_set_config(wifi_interface_t, wifi_config_t*); esp_err_t esp_wifi_start(); esp_err_t esp_wifi_connect();
esp_err_t esp_wifi_disconnect(); esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*, bool);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*, wifi_ap_record_t*);
