#pragma once
#include <sys/time.h>
enum { SNTP_OPMODE_POLL = 0 };
void esp_sntp_setoperatingmode(int); void esp_sntp_setservername(int, const char*); void sntp_set_sync_interval(uint32_t);
void sntp_set_time_sync_notification_cb(void (*)(struct timeval*)); void esp_sntp_init(); bool sntp_restart();
