#pragma once
#define ESP_LOGI(t, ...) printf(__VA_ARGS__)
#define ESP_LOGE(t, ...) printf(__VA_ARGS__)
#define ESP_LOGW(t, ...) printf(__VA_ARGS__)
#include <cstdarg>
typedef int (*vprintf_like_t)(const char*, va_list); vprintf_like_t esp_log_set_vprintf(vprintf_like_t);
#define ESP_LOGD(t, ...) printf(__VA_ARGS__)
