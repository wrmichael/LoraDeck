#pragma once
// stub: only what board_hw.cpp uses (not ESP-IDF)
#include "driver/uart.h"
typedef void* adc_oneshot_unit_handle_t;
typedef enum { ADC_UNIT_1 } adc_unit_t;
typedef enum { ADC_CHANNEL_9 = 9 } adc_channel_t;
typedef enum { ADC_ATTEN_DB_12 = 3 } adc_atten_t;
typedef enum { ADC_BITWIDTH_DEFAULT = 0 } adc_bitwidth_t;
struct adc_oneshot_unit_init_cfg_t { adc_unit_t unit_id; int clk_src; int ulp_mode; };
struct adc_oneshot_chan_cfg_t { adc_atten_t atten; adc_bitwidth_t bitwidth; };
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*, adc_oneshot_unit_handle_t*);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t, const adc_oneshot_chan_cfg_t*);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t, int*);
