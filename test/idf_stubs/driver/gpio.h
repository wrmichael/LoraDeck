#pragma once
#include <cstdint>
typedef int gpio_num_t; enum { GPIO_MODE_INPUT, GPIO_INTR_POSEDGE };
struct gpio_config_t { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; };
int gpio_config(const gpio_config_t*); int gpio_install_isr_service(int); int gpio_isr_handler_add(gpio_num_t, void(*)(void*), void*);
enum { GPIO_MODE_OUTPUT = 2 };
#define GPIO_NUM_8 8
#define GPIO_NUM_9 9
#define GPIO_NUM_10 10
#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14
int gpio_set_level(gpio_num_t,int); int gpio_get_level(gpio_num_t); int gpio_set_intr_type(gpio_num_t,int);
#define GPIO_NUM_0 0
enum { GPIO_PULLUP_ENABLE = 1 };
