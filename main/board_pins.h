// Which board this firmware is built for (CMake: -DLORADECK_BOARD=heltec_v3 | cardputer_adv) and its pins.
#pragma once
#include "driver/gpio.h"

#if defined(LORADECK_CARDPUTER)
// M5Stack Cardputer-Adv (Stamp-S3A, ESP32-S3FN8, 8 MB) with the Cap LoRa-1262 (SX1262, TCXO 1.8 V on DIO3,
// RF switch on DIO2, antenna switch enabled through a PI4IOE5V6408 at I2C 0x43 pin P0). Pins from M5Stack's
// pin map and Meshtastic's m5stack_cardputer_adv variant.
#define LD_BOARD_ID "cardputer_adv"
#define LD_BOARD_NAME "Cardputer-Adv + Cap LoRa-1262"
static const gpio_num_t LD_LORA_NSS = GPIO_NUM_5, LD_LORA_SCK = GPIO_NUM_40, LD_LORA_MOSI = GPIO_NUM_14,
                        LD_LORA_MISO = GPIO_NUM_39, LD_LORA_RST = GPIO_NUM_3, LD_LORA_BUSY = GPIO_NUM_6,
                        LD_LORA_DIO1 = GPIO_NUM_4;
static const gpio_num_t LD_I2C_SDA = GPIO_NUM_8, LD_I2C_SCL = GPIO_NUM_9;          // PI4IO, keyboard (TCA8418)
static const gpio_num_t LD_TFT_MOSI = GPIO_NUM_35, LD_TFT_SCK = GPIO_NUM_36, LD_TFT_CS = GPIO_NUM_37,
                        LD_TFT_DC = GPIO_NUM_34, LD_TFT_RST = GPIO_NUM_33, LD_TFT_BL = GPIO_NUM_38;
#else
// Heltec WiFi LoRa 32 V3
#define LD_BOARD_ID "heltec_v3"
#define LD_BOARD_NAME "Heltec WiFi LoRa 32 V3"
static const gpio_num_t LD_LORA_NSS = GPIO_NUM_8, LD_LORA_SCK = GPIO_NUM_9, LD_LORA_MOSI = GPIO_NUM_10,
                        LD_LORA_MISO = GPIO_NUM_11, LD_LORA_RST = GPIO_NUM_12, LD_LORA_BUSY = GPIO_NUM_13,
                        LD_LORA_DIO1 = GPIO_NUM_14;
#endif
