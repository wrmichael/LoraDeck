#include "oled.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "font6x8.h"
#include "board_pins.h"

// The status screen is a 128x64 one-bit picture (8 pages of 128 columns, as the SSD1306 stores it). The Heltec
// shows it on its OLED; the Cardputer scales it to 240x120 on its 240x135 colour LCD (ST7789).
static uint8_t s_fb[8][128];
static bool s_ok = false;

#if defined(LORADECK_CARDPUTER)
#include "driver/spi_master.h"
#include "freertos/semphr.h"
static spi_device_handle_t s_tft;
static SemaphoreHandle_t s_mx;                               // the status task and the keyboard screens share the LCD
static const uint16_t FG = 0xA0FD, BG = 0x0000;              // amber on black, RGB565 sent high byte first

#include "audiowide.h"
// The title band: 240 x kAwHeight coverage (0-15), built when the title changes, drawn over fb rows 0-15.
static uint8_t s_tband[kAwHeight][240];
static uint16_t s_tpal[16];
static char s_title[16] = "";
static bool s_haveTitle = false;

bool oled::title(const char* t) {
    if (!t) t = "";
    if (!strncmp(t, s_title, sizeof s_title - 1) && s_haveTitle == (*t != 0)) return true;
    strlcpy(s_title, t, sizeof s_title);
    memset(s_tband, 0, sizeof s_tband);
    int idx[16], n = 0, total = 0;
    for (const char* c = s_title; *c && n < 15; c++) {
        const char* p = strchr(kAwChars, *c);
        if (!p) continue;
        idx[n++] = (int)(p - kAwChars);
        total += kAwGlyphs[idx[n - 1]].w;
    }
    s_haveTitle = n > 0;
    const int x0 = 4, avail = 232;                           // a long call is narrowed to fit
    int w = total > avail ? avail : total;
    for (int x = 0; x < w; x++) {
        int sx = total > avail ? x * total / avail : x, g = 0;
        while (g < n && sx >= kAwGlyphs[idx[g]].w) sx -= kAwGlyphs[idx[g++]].w;
        if (g >= n) break;
        const AwGlyph& gl = kAwGlyphs[idx[g]];
        for (int y = 0; y < kAwHeight; y++) {
            int k = y * gl.w + sx;
            uint8_t b = kAwData[gl.off + k / 2];
            s_tband[y][x0 + x] = (k & 1) ? (b & 15) : (b >> 4);
        }
    }
    for (int a = 0; a < 16; a++) {                           // gold (255, 215, 0) faded to black, RGB565 swapped
        uint16_t c = (uint16_t)(((31 * a / 15) << 11) | ((53 * a / 15) << 5));
        s_tpal[a] = (uint16_t)((c >> 8) | (c << 8));
    }
    return true;
}

static void tftSend(const void* d, int n, bool data) {
    if (n <= 0) return;
    gpio_set_level(LD_TFT_DC, data ? 1 : 0);
    spi_transaction_t t = {};
    t.length = (size_t)n * 8;
    t.tx_buffer = d;
    spi_device_polling_transmit(s_tft, &t);
}
static void tftCmd(uint8_t c, const uint8_t* d = nullptr, int n = 0) {
    tftSend(&c, 1, false);
    if (n) tftSend(d, n, true);
}
// landscape, the panel's visible 240x135 window starts at column 40, row 53 of its 320x240 memory
static void tftWindow(int x0, int y0, int x1, int y1) {
    x0 += 40; x1 += 40; y0 += 53; y1 += 53;
    uint8_t ca[] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t ra[] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    tftCmd(0x2A, ca, 4);
    tftCmd(0x2B, ra, 4);
    tftCmd(0x2C);
}

bool oled::begin() {
    gpio_config_t o = {};
    o.pin_bit_mask = (1ULL << LD_TFT_DC) | (1ULL << LD_TFT_RST) | (1ULL << LD_TFT_BL);
    o.mode = GPIO_MODE_OUTPUT;
    gpio_config(&o);
    spi_bus_config_t bus = {};
    bus.mosi_io_num = LD_TFT_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = LD_TFT_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 240 * 2 + 16;
    s_mx = xSemaphoreCreateMutex();
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = 40000000;
    dev.mode = 0;
    dev.spics_io_num = LD_TFT_CS;
    dev.queue_size = 1;
    if (spi_bus_add_device(SPI3_HOST, &dev, &s_tft) != ESP_OK) return false;
    gpio_set_level(LD_TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LD_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    tftCmd(0x01);                                            // software reset
    vTaskDelay(pdMS_TO_TICKS(150));
    tftCmd(0x11);                                            // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    static const uint8_t colmod = 0x55, madctl = 0x60;      // 16-bit colour; landscape (MV | MX)
    tftCmd(0x3A, &colmod, 1);
    tftCmd(0x36, &madctl, 1);
    tftCmd(0x21);                                            // colours inverted on this panel
    tftCmd(0x13);
    tftCmd(0x29);                                            // display on
    static uint16_t line[240];
    for (int i = 0; i < 240; i++) line[i] = BG;
    tftWindow(0, 0, 239, 134);
    for (int y = 0; y < 135; y++) tftSend(line, sizeof line, true);
    gpio_set_level(LD_TFT_BL, 1);                            // backlight on
    s_ok = true;
    clear();
    show();
    return true;
}

void oled::show() {
    if (!s_ok) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    static uint16_t line[240];
    tftWindow(0, 7, 239, 126);                               // 240 x 120, centred in the 135 rows
    for (int y = 0; y < 120; y++) {
        if (s_haveTitle && y < kAwHeight) {                  // fb rows 0-15 (the big text) become the title
            for (int x = 0; x < 240; x++) line[x] = s_tpal[s_tband[y][x]];
            tftSend(line, sizeof line, true);
            continue;
        }
        int sy = y * 64 / 120;
        const uint8_t* page = s_fb[sy >> 3];
        uint8_t bit = (uint8_t)(1u << (sy & 7));
        for (int x = 0; x < 240; x++) line[x] = (page[x * 128 / 240] & bit) ? FG : BG;
        tftSend(line, sizeof line, true);
    }
    xSemaphoreGive(s_mx);
}

void oled::frame(void (*fill)(int y, uint16_t* line, void* ctx), void* ctx) {
    if (!s_ok) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    static uint16_t line[240];
    tftWindow(0, 0, 239, 134);
    for (int y = 0; y < 135; y++) {
        fill(y, line, ctx);
        tftSend(line, sizeof line, true);
    }
    xSemaphoreGive(s_mx);
}

void oled::power(bool on) {
    if (s_ok) gpio_set_level(LD_TFT_BL, on ? 1 : 0);
}

#else
static const gpio_num_t P_SDA = GPIO_NUM_17, P_SCL = GPIO_NUM_18, P_RST = GPIO_NUM_21, P_VEXT = GPIO_NUM_36;
static i2c_master_dev_handle_t s_dev;

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

#endif

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

#if !defined(LORADECK_CARDPUTER)
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

void oled::frame(void (*)(int, uint16_t*, void*), void*) {}
bool oled::title(const char*) { return false; }

void oled::power(bool on) {
    static const uint8_t onc[] = {0xAF}, offc[] = {0xAE};
    if (s_ok) cmds(on ? onc : offc, 1);
}
#endif
