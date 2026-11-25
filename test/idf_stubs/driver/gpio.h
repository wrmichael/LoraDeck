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
#define GPIO_NUM_3 3
#define GPIO_NUM_4 4
#define GPIO_NUM_5 5
#define GPIO_NUM_6 6
#define GPIO_NUM_17 17
#define GPIO_NUM_18 18
#define GPIO_NUM_21 21
#define GPIO_NUM_33 33
#define GPIO_NUM_34 34
#define GPIO_NUM_35 35
#define GPIO_NUM_36 36
#define GPIO_NUM_37 37
#define GPIO_NUM_38 38
#define GPIO_NUM_39 39
#define GPIO_NUM_40 40
