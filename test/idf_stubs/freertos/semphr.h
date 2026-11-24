#pragma once
SemaphoreHandle_t xSemaphoreCreateMutex(); int xSemaphoreTake(SemaphoreHandle_t,int); int xSemaphoreGive(SemaphoreHandle_t);
SemaphoreHandle_t xSemaphoreCreateBinary(); int xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*);
