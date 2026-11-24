#pragma once
void vTaskDelay(int); int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*);
