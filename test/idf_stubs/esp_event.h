#pragma once
#include <cstdint>
#include "driver/uart.h"
typedef const char* esp_event_base_t;
extern esp_event_base_t WIFI_EVENT, IP_EVENT;
enum { ESP_EVENT_ANY_ID = -1, IP_EVENT_STA_GOT_IP = 0 };
typedef void (*esp_event_handler_t)(void*, esp_event_base_t, int32_t, void*);
esp_err_t esp_event_loop_create_default(); esp_err_t esp_event_handler_register(esp_event_base_t, int32_t, esp_event_handler_t, void*);
