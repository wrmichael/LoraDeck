#include "oled.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "font6x8.h"

static const gpio_num_t P_SDA = GPIO_NUM_17, P_SCL = GPIO_NUM_18, P_RST = GPIO_NUM_21, P_VEXT = GPIO_NUM_36;
static i2c_master_dev_handle_t s_dev;
static uint8_t s_fb[8][128];
static bool s_ok = false;

static bool cmds(const uint8_t* c, int n) {
    uint8_t b[32];
    b[0] = 0x00;                                             // control byte: commands follow
    memcpy(b + 1, c, n);
    return i2c_master_transmit(s_dev, b, n + 1, 100) == ESP_OK;
}

bool oled::begin() {
    gpio_config_t o = {};
    o.pin_bit_mask = (1ULL << P_RST) | (1ULL << P_VEXT);
    o.mode = GPIO_MODE_OUTPUT;
    gpio_config(&o);
    gpio_set_level(P_VEXT, 0);                               // Vext on
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(P_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(P_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = P_SDA;
    bc.scl_io_num = P_SCL;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bc, &bus) != ESP_OK) return false;
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = 0x3C;
    dc.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) return false;
    static const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
                                   0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    s_ok = cmds(init, sizeof init);
    clear();
    show();
    return s_ok;
}

void oled::clear() { memset(s_fb, 0, sizeof s_fb); }

void oled::text(int col, int row, const char* s, bool invert) {
    if (row < 0 || row > 7) return;
    for (int x = col * 6; *s && x <= 122; s++, x += 6) {
        unsigned char c = (unsigned char)*s;
        const uint8_t* g = kFont6x8[(c < 32 || c > 126) ? 0 : c - 32];
        for (int i = 0; i < 6; i++) s_fb[row][x + i] = invert ? (uint8_t)~g[i] : g[i];
    }
}

void oled::bigText(int col, int row, const char* s) {
    if (row < 0 || row > 6) return;
    for (int x = col * 12; *s && x <= 116; s++, x += 12) {
        unsigned char c = (unsigned char)*s;
        const uint8_t* g = kFont6x8[(c < 32 || c > 126) ? 0 : c - 32];
        for (int i = 0; i < 6; i++) {
            uint16_t v = 0;                                  // stretch 8 rows to 16
            for (int b = 0; b < 8; b++) if (g[i] >> b & 1) v |= 3u << (2 * b);
            for (int d = 0; d < 2; d++) {
                s_fb[row][x + 2 * i + d] = (uint8_t)v;
                s_fb[row + 1][x + 2 * i + d] = (uint8_t)(v >> 8);
            }
        }
    }
}

void oled::show() {
    if (!s_ok) return;
    static const uint8_t win[] = {0x21, 0, 127, 0x22, 0, 7};
    cmds(win, sizeof win);
    uint8_t b[129];
    b[0] = 0x40;                                             // control byte: data follows
    for (int p = 0; p < 8; p++) {
        memcpy(b + 1, s_fb[p], 128);
        i2c_master_transmit(s_dev, b, sizeof b, 100);
    }
}

void oled::power(bool on) {
    static const uint8_t onc[] = {0xAF}, offc[] = {0xAE};
    if (s_ok) cmds(on ? onc : offc, 1);
}
