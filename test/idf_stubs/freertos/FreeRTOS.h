#pragma once
#include <cstddef>
typedef int portMUX_TYPE; typedef void* SemaphoreHandle_t; typedef int BaseType_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL_SAFE(x) (void)(x)
#define portEXIT_CRITICAL_SAFE(x) (void)(x)
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define pdFALSE 0
#define portYIELD_FROM_ISR() do{}while(0)
#ifndef portMAX_DELAY_DEF
#define portMAX_DELAY_DEF
#endif
