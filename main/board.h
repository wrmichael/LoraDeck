// Board-level pieces (Heltec WiFi LoRa 32 V3) shared between main.cpp, net.cpp, web.cpp and ota.cpp.
#pragma once
#include <string>
#include "app.h"
#include "esp_http_server.h"

void net_start(App& app);
std::string net_status();
std::string net_ip();
std::string net_ssid();          // the hotspot name in use
void web_start(App& app);
void ota_register(httpd_handle_t srv, App& app);
void ota_start();
bool board_init();               // board-specific start-up before the radio (Cardputer: Cap LoRa antenna switch)
const char* board_id();          // "heltec_v3" or "cardputer_adv"
const char* board_name();
void gps_start(App& app);        // optional GPS module (settings gpsRx/gpsTx/gpsBaud/gpsPps)
std::string gps_status();                // mark a freshly updated firmware as good after a minute
extern volatile bool g_rebootReq;
extern volatile bool g_storageBad;   // the storage looks damaged (checked at start)
