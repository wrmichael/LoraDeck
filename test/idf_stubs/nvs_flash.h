#pragma once
#include "driver/uart.h"
#define ESP_ERR_NVS_NO_FREE_PAGES 1
#define ESP_ERR_NVS_NEW_VERSION_FOUND 2
esp_err_t nvs_flash_init(); esp_err_t nvs_flash_erase();
