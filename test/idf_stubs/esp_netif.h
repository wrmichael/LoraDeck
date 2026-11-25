#pragma once
#include <cstdint>
#include "driver/uart.h"
#include "esp_event.h"
typedef struct esp_netif_obj esp_netif_t;
struct esp_ip4_addr_t { uint32_t addr; };
struct esp_netif_ip_info_t { esp_ip4_addr_t ip, netmask, gw; };
struct ip_event_got_ip_t { esp_netif_t* esp_netif; esp_netif_ip_info_t ip_info; };
#define IPSTR "%d.%d.%d.%d"
#define IP2STR(a) (int)((a)->addr & 0xff), (int)(((a)->addr >> 8) & 0xff), (int)(((a)->addr >> 16) & 0xff), (int)(((a)->addr >> 24) & 0xff)
esp_err_t esp_netif_init(); esp_netif_t* esp_netif_create_default_wifi_ap(); esp_netif_t* esp_netif_create_default_wifi_sta();
esp_err_t esp_netif_set_hostname(esp_netif_t*, const char*);
