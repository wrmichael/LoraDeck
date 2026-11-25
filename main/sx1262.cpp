#include "sx1262.h"
#include "board_pins.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "morse.h"

static const char* TAG = "sx1262";

// Heltec WiFi LoRa 32 V3 wiring
static const gpio_num_t P_NSS = LD_LORA_NSS, P_SCK = LD_LORA_SCK, P_MOSI = LD_LORA_MOSI, P_MISO = LD_LORA_MISO,
                        P_RST = LD_LORA_RST, P_BUSY = LD_LORA_BUSY, P_DIO1 = LD_LORA_DIO1;   // board_pins.h

// commands (SX126x datasheet, section 13)
enum : uint8_t {
    C_SET_STANDBY = 0x80, C_SET_RX = 0x82, C_SET_TX = 0x83, C_CALIBRATE = 0x89, C_CAL_IMAGE = 0x98,
    C_SET_PA = 0x95, C_SET_REGULATOR = 0x96, C_WRITE_REG = 0x0D, C_READ_REG = 0x1D, C_WRITE_BUF = 0x0E,
    C_READ_BUF = 0x1E, C_SET_IRQ = 0x08, C_GET_IRQ = 0x12, C_CLR_IRQ = 0x02, C_DIO2_RFSW = 0x9D,
    C_DIO3_TCXO = 0x97, C_SET_FREQ = 0x86, C_SET_PKT_TYPE = 0x8A, C_SET_TX_PARAMS = 0x8E,
    C_SET_MOD = 0x8B, C_SET_PKT = 0x8C, C_SET_BUF_BASE = 0x8F, C_GET_RX_BUF = 0x13, C_GET_PKT_STATUS = 0x14,
    C_GET_STATUS = 0xC0, C_SET_TX_CW = 0xD1,
};
enum : uint16_t { IRQ_TX_DONE = 0x0001, IRQ_RX_DONE = 0x0002, IRQ_HDR_ERR = 0x0020, IRQ_CRC_ERR = 0x0040, IRQ_TIMEOUT = 0x0200 };
static const uint16_t REG_SYNC = 0x0740, REG_OCP = 0x08E7, REG_IQ = 0x0736;

static spi_device_handle_t s_spi;
static SemaphoreHandle_t s_irq, s_mtx;
static LoraConfig s_cfg;
static std::atomic<bool> s_sending{false};
static sx1262::Stats s_stats;
static SemaphoreHandle_t s_cwGo;
static std::string s_cwText;
static int s_cwWpm = 20;
static std::atomic<bool> s_cwAbort{false};
std::function<void(const uint8_t*, int, int, float)> sx1262::onReceive;
std::function<void()> sx1262::onSent;

static void waitBusy() {
    for (int i = 0; i < 2000 && gpio_get_level(P_BUSY); i++) esp_rom_delay_us(50);
}

// One command: opcode + parameters out, optional reply in (after `skip` status bytes).
static void cmd(uint8_t op, const uint8_t* out, int nOut, uint8_t* in = nullptr, int nIn = 0, int skip = 1) {
    uint8_t tx[300] = {}, rx[300] = {};
    int n = 1 + nOut + (nIn ? skip + nIn : 0);
    tx[0] = op;
    if (nOut) memcpy(tx + 1, out, nOut);
    waitBusy();
    gpio_set_level(P_NSS, 0);
    spi_transaction_t t = {};
    t.length = n * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(P_NSS, 1);
    if (nIn) memcpy(in, rx + 1 + nOut + skip, nIn);
}
static void cmd1(uint8_t op, uint8_t v) { cmd(op, &v, 1); }
static void writeReg(uint16_t addr, const uint8_t* d, int n) {
    uint8_t b[16] = {(uint8_t)(addr >> 8), (uint8_t)addr};
    memcpy(b + 2, d, n);
    cmd(C_WRITE_REG, b, 2 + n);
}
static uint8_t readReg(uint16_t addr) {
    uint8_t a[2] = {(uint8_t)(addr >> 8), (uint8_t)addr}, v = 0;
    cmd(C_READ_REG, a, 2, &v, 1, 1);
    return v;
}
static uint16_t irqStatus() {
    uint8_t r[2] = {};
    cmd(C_GET_IRQ, nullptr, 0, r, 2, 1);
    return (uint16_t)(r[0] << 8 | r[1]);
}
static void clearIrq(uint16_t m) { uint8_t b[2] = {(uint8_t)(m >> 8), (uint8_t)m}; cmd(C_CLR_IRQ, b, 2); }

static uint8_t bwCode(int khz) { return khz >= 500 ? 0x06 : khz >= 250 ? 0x05 : 0x04; }
static bool ldro(const LoraConfig& c) { return (double)(1 << c.sf) / (c.bwKhz * 1000.0) * 1000.0 > 16.0; }

static void packetParams(uint8_t len) {
    uint8_t p[6] = {0x00, 0x10, 0x00, len, 0x01, 0x00};   // preamble 16, explicit header, length, CRC on, IQ normal
    cmd(C_SET_PKT, p, 6);
}
static void startRx() {
    uint8_t sb = 0x00;
    cmd(C_SET_STANDBY, &sb, 1);
    packetParams(255);
    uint8_t to[3] = {0xFF, 0xFF, 0xFF};                    // continuous receive
    cmd(C_SET_RX, to, 3);
}

static void applyConfig(const LoraConfig& c) {
    uint8_t sb = 0x00;
    cmd(C_SET_STANDBY, &sb, 1);
    uint8_t ci[2];                                           // image calibration for the band in use
    if (c.freqHz > 900000000) { ci[0] = 0xE1; ci[1] = 0xE9; }
    else if (c.freqHz > 850000000) { ci[0] = 0xD7; ci[1] = 0xDB; }
    else if (c.freqHz > 770000000) { ci[0] = 0xC1; ci[1] = 0xC5; }
    else if (c.freqHz > 460000000) { ci[0] = 0x75; ci[1] = 0x81; }
    else if (c.freqHz > 425000000) { ci[0] = 0x6B; ci[1] = 0x6F; }
    else { ci[0] = 0x29; ci[1] = 0x2B; }
    cmd(C_CAL_IMAGE, ci, 2);
    uint32_t f = (uint32_t)((uint64_t)c.freqHz * (1u << 25) / 32000000u);
    uint8_t fb[4] = {(uint8_t)(f >> 24), (uint8_t)(f >> 16), (uint8_t)(f >> 8), (uint8_t)f};
    cmd(C_SET_FREQ, fb, 4);
    uint8_t pa[4] = {0x04, 0x07, 0x00, 0x01};               // SX1262 high-power PA, up to +22 dBm
    cmd(C_SET_PA, pa, 4);
    int p = c.powerDbm < -9 ? -9 : c.powerDbm > 22 ? 22 : c.powerDbm;
    uint8_t tp[2] = {(uint8_t)(int8_t)p, 0x04};             // ramp 200 us
    cmd(C_SET_TX_PARAMS, tp, 2);
    uint8_t ocp = 0x38;                                      // 140 mA current limit
    writeReg(REG_OCP, &ocp, 1);
    uint8_t mod[4] = {(uint8_t)c.sf, bwCode(c.bwKhz), (uint8_t)(c.cr - 4), (uint8_t)(ldro(c) ? 1 : 0)};
    cmd(C_SET_MOD, mod, 4);
    uint8_t iq = readReg(REG_IQ) | 0x04;                     // datasheet 15.4: IQ polarity fix (normal IQ)
    writeReg(REG_IQ, &iq, 1);
    uint8_t sync[2] = {0x14, 0x24};                          // "private network" sync word
    writeReg(REG_SYNC, sync, 2);
    s_cfg = c;
    startRx();
    ESP_LOGI(TAG, "radio: %.3f MHz SF%d BW%d CR4/%d %d dBm, sync 0x1424, listening", c.freqHz / 1e6, c.sf, c.bwKhz,
             c.cr, c.powerDbm);
}

static void IRAM_ATTR dio1Isr(void*) {
    BaseType_t w = pdFALSE;
    xSemaphoreGiveFromISR(s_irq, &w);
    if (w) portYIELD_FROM_ISR();
}

static void radioTask(void*) {
    for (;;) {
        xSemaphoreTake(s_irq, pdMS_TO_TICKS(1000));
        if (!gpio_get_level(P_DIO1)) continue;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        uint16_t irq = irqStatus();
        clearIrq(0xFFFF);
        uint8_t buf[256];
        int len = 0, rssi = 0;
        float snr = 0;
        bool got = false, sent = false, bad = false;
        if (irq & IRQ_TX_DONE) {
            s_sending = false;
            s_stats.tx++;
            sent = true;
            startRx();
        }
        if (irq & IRQ_RX_DONE) {
            if (irq & (IRQ_CRC_ERR | IRQ_HDR_ERR)) {
                s_stats.rxCrc++;
                bad = true;
            } else {
                uint8_t st[2] = {};
                cmd(C_GET_RX_BUF, nullptr, 0, st, 2, 1);
                len = st[0];
                uint8_t off[2] = {st[1], 0};
                if (len > 0) cmd(C_READ_BUF, off, 1, buf, len, 1);
                uint8_t ps[3] = {};
                cmd(C_GET_PKT_STATUS, nullptr, 0, ps, 3, 1);
                rssi = -(int)ps[0] / 2;
                snr = (int8_t)ps[1] / 4.0f;
                s_stats.rx++;
                s_stats.lastRssi = rssi;
                s_stats.lastSnr = snr;
                got = len > 0;
            }
        }
        xSemaphoreGive(s_mtx);
        if (sent) ESP_LOGI(TAG, "TX done (#%u)", (unsigned)s_stats.tx);
        if (bad) ESP_LOGW(TAG, "RX error: %s (a packet was there but did not decode)",
                          (irq & IRQ_HDR_ERR) ? "header" : "CRC");
        if (got) {
            char hex[3 * 16 + 1] = {};
            for (int i = 0; i < len && i < 16; i++) snprintf(hex + 3 * i, 4, "%02x ", buf[i]);
            ESP_LOGI(TAG, "RX %d bytes, RSSI %d dBm, SNR %.1f dB: %s%s", len, rssi, snr, hex, len > 16 ? "..." : "");
        }
        if (sent && sx1262::onSent) sx1262::onSent();
        if (got && sx1262::onReceive) sx1262::onReceive(buf, len, rssi, snr);
    }
}

// CW: the carrier on (TX continuous wave) and off (standby with the TCXO running, so it starts again at once).
// A slower PA ramp (800 us) for softer key clicks.
static void cwTask(void*) {
    for (;;) {
        xSemaphoreTake(s_cwGo, portMAX_DELAY);
        int unit = 1200 / (s_cwWpm > 0 ? s_cwWpm : 20);
        int p = s_cfg.powerDbm < -9 ? -9 : s_cfg.powerDbm > 22 ? 22 : s_cfg.powerDbm;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        uint8_t tp[2] = {(uint8_t)(int8_t)p, 0x05};
        cmd(C_SET_TX_PARAMS, tp, 2);
        uint8_t xosc = 0x01;
        cmd(C_SET_STANDBY, &xosc, 1);
        int64_t t0 = esp_timer_get_time();
        std::string t = morseClean(s_cwText);
        for (size_t i = 0; i < t.size() && !s_cwAbort; i++) {
            if (t[i] == ' ') { vTaskDelay(pdMS_TO_TICKS(unit * 4)); continue; }
            for (const char* e = morseOf(t[i]); e && *e && !s_cwAbort; e++) {
                cmd(C_SET_TX_CW, nullptr, 0);
                vTaskDelay(pdMS_TO_TICKS(unit * (*e == '.' ? 1 : 3)));
                cmd(C_SET_STANDBY, &xosc, 1);
                vTaskDelay(pdMS_TO_TICKS(unit));
            }
            vTaskDelay(pdMS_TO_TICKS(unit * 2));
        }
        cmd(C_SET_STANDBY, &xosc, 1);
        tp[1] = 0x04;
        cmd(C_SET_TX_PARAMS, tp, 2);
        startRx();
        s_stats.tx++;
        bool aborted = s_cwAbort;
        s_cwAbort = false;
        s_sending = false;
        xSemaphoreGive(s_mtx);
        ESP_LOGI(TAG, "CW %s after %lld ms: %s", aborted ? "stopped" : "done", (long long)((esp_timer_get_time() - t0) / 1000), t.c_str());
        if (sx1262::onSent) sx1262::onSent();
    }
}

bool sx1262::sendCw(const std::string& text, int wpm) {
    if (!s_cwGo || text.empty() || s_sending.exchange(true)) return false;
    s_cwText = text;
    s_cwWpm = wpm;
    s_cwAbort = false;
    ESP_LOGI(TAG, "CW on %.3f MHz at %d wpm: %s", s_cfg.freqHz / 1e6, wpm, text.c_str());
    xSemaphoreGive(s_cwGo);
    return true;
}

void sx1262::abortTx() { s_cwAbort = true; }

bool sx1262::begin(const LoraConfig& c) {
    gpio_config_t o = {};
    o.pin_bit_mask = (1ULL << P_NSS) | (1ULL << P_RST);
    o.mode = GPIO_MODE_OUTPUT;
    gpio_config(&o);
    gpio_set_level(P_NSS, 1);
    gpio_config_t i = {};
    i.pin_bit_mask = (1ULL << P_BUSY) | (1ULL << P_DIO1);
    i.mode = GPIO_MODE_INPUT;
    gpio_config(&i);
    spi_bus_config_t bus = {};
    bus.mosi_io_num = P_MOSI;
    bus.miso_io_num = P_MISO;
    bus.sclk_io_num = P_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 300;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = 8 * 1000 * 1000;
    dev.mode = 0;
    dev.spics_io_num = -1;                                   // NSS by hand (BUSY must be checked first)
    dev.queue_size = 1;
    if (spi_bus_add_device(SPI2_HOST, &dev, &s_spi) != ESP_OK) return false;

    gpio_set_level(P_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(P_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    waitBusy();
    uint8_t st = 0;
    cmd(C_GET_STATUS, nullptr, 0, &st, 1, 0);
    if (st == 0x00 || st == 0xFF) { ESP_LOGE(TAG, "SX1262 does not answer (status %02x)", st); return false; }
    uint8_t sb = 0x00;
    cmd(C_SET_STANDBY, &sb, 1);
    uint8_t tcxo[4] = {0x02, 0x00, 0x01, 0x40};              // TCXO 1.8 V on DIO3, 5 ms start-up
    cmd(C_DIO3_TCXO, tcxo, 4);
    cmd1(C_CALIBRATE, 0x7F);
    vTaskDelay(pdMS_TO_TICKS(5));
    waitBusy();
    cmd1(C_SET_REGULATOR, 0x01);                             // DC-DC
    cmd1(C_DIO2_RFSW, 0x01);                                 // DIO2 drives the antenna switch
    cmd1(C_SET_PKT_TYPE, 0x01);                              // LoRa
    uint8_t base[2] = {0x00, 0x00};
    cmd(C_SET_BUF_BASE, base, 2);
    uint8_t irq[8] = {0x02, 0x63, 0x02, 0x63, 0, 0, 0, 0};    // TX/RX done, header/CRC error, timeout on DIO1
    cmd(C_SET_IRQ, irq, 8);
    s_irq = xSemaphoreCreateBinary();
    s_mtx = xSemaphoreCreateMutex();
    gpio_set_intr_type(P_DIO1, GPIO_INTR_POSEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(P_DIO1, dio1Isr, nullptr);
    xTaskCreate(radioTask, "lora", 4096, nullptr, 10, nullptr);
    s_cwGo = xSemaphoreCreateBinary();
    xTaskCreate(cwTask, "cw", 3072, nullptr, 9, nullptr);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    applyConfig(c);
    xSemaphoreGive(s_mtx);
    ESP_LOGI(TAG, "SX1262 ready: %.3f MHz SF%d BW%d CR4/%d %d dBm", c.freqHz / 1e6, c.sf, c.bwKhz, c.cr, c.powerDbm);
    return true;
}

void sx1262::configure(const LoraConfig& c) {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    applyConfig(c);
    s_sending = false;
    xSemaphoreGive(s_mtx);
}

bool sx1262::send(const uint8_t* data, int len) {
    if (len <= 0 || len > 255 || s_sending.exchange(true)) return false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint8_t sb = 0x00;
    cmd(C_SET_STANDBY, &sb, 1);
    uint8_t b[256];
    b[0] = 0x00;                                             // buffer offset
    memcpy(b + 1, data, len);
    cmd(C_WRITE_BUF, b, 1 + len);
    packetParams((uint8_t)len);
    uint8_t to[3] = {0, 0, 0};                               // no timeout
    cmd(C_SET_TX, to, 3);
    LoraConfig c = s_cfg;
    xSemaphoreGive(s_mtx);
    ESP_LOGI(TAG, "TX %d bytes on %.3f MHz SF%d BW%d, %d ms on air", len, c.freqHz / 1e6, c.sf, c.bwKhz, airtimeMs(c, len));
    return true;
}

int sx1262::airtimeMs(const LoraConfig& c, int len) {
    double ts = (double)(1 << c.sf) / (c.bwKhz * 1000.0);
    double de = ldro(c) ? 1 : 0;
    double n = 8 + std::max(std::ceil((8.0 * len - 4 * c.sf + 28 + 16) / (4 * (c.sf - 2 * de))) * c.cr, 0.0);
    return (int)((16 + 4.25 + n) * ts * 1000.0 + 0.5);
}

sx1262::Stats sx1262::stats() { return s_stats; }
