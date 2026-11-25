// Board-level pieces (Heltec WiFi LoRa 32 V3) shared between main.cpp, net.cpp, web.cpp and ota.cpp.
#pragma once
#include <string>
#include <vector>
#include "app.h"
#include "esp_http_server.h"

void net_start(App& app);
std::string net_status();
std::string net_ip();
std::string net_ssid();          // the hotspot name in use
std::string net_state();         // home network: its address, "joining", "retrying" or "not set"
struct WifiNet { std::string ssid; int rssi; bool open; };
int net_scan(std::vector<WifiNet>& out);   // networks in reach, strongest first; -1 if the scan failed
void net_join();                 // join cfg.staSsid now (empty: leave the home network)
void net_hotspot();              // apply cfg.apSsid / apPass now
void web_start(App& app);
void ota_register(httpd_handle_t srv, App& app);
void ota_start();
bool board_init();               // board-specific start-up before the radio (Cardputer: Cap LoRa antenna switch)
const char* board_id();          // "heltec_v3" or "cardputer_adv"
const char* board_name();
int board_battery_mv();          // battery voltage, mV; -1 if not known (Cardputer only for now)
int board_battery_pct();         // 0..100, -1 if not known
void gps_start(App& app);        // optional GPS module (settings gpsRx/gpsTx/gpsBaud/gpsPps)
std::string gps_status();                // mark a freshly updated firmware as good after a minute
extern volatile bool g_rebootReq;
extern volatile bool g_storageBad;   // the storage looks damaged (checked at start)
