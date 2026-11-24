// Optional GPS module on a UART (NMEA, e.g. a NEO-6M / GT-U7 / ATGM336H wired to two free pins): sets the
// clock from RMC sentences and, if gpsGrid is on, the grid locator from the position.
// Without a PPS pin the time is taken as "the second in the RMC started when its burst of sentences began",
// good to some tens of ms (modules send the burst shortly after the second). With a PPS pin the second starts
// exactly at the pulse.
#include "board.h"
#include "nmea.h"
#include "clock.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "gps";
static App* s_app;
static int s_baud = 9600;
static bool s_pps = false;
static std::atomic<int64_t> s_ppsUs{0};
static std::atomic<int64_t> s_lastByteUs{0};
static Nmea s_nmea;                       // the GPS task only; status() reads copies of small fields
static std::atomic<int> s_quality{0}, s_sats{0};
static std::atomic<bool> s_timeOk{false};
static std::string s_grid;                // set by the task; read for the status line (short, rarely changes)

static void IRAM_ATTR ppsIsr(void*) { s_ppsUs = esp_timer_get_time(); }

static void gpsTask(void*) {
    uint8_t b[64];
    int64_t burstUs = 0, lastGridUs = -60000000LL, lastSetUs = -3600000000LL;
    for (;;) {
        int r = uart_read_bytes(UART_NUM_1, b, sizeof b, pdMS_TO_TICKS(10));
        if (r <= 0) continue;
        int64_t nowUs = esp_timer_get_time();
        int64_t firstUs = nowUs - (int64_t)r * 10 * 1000000 / s_baud;   // when the first of these bytes arrived
        if (nowUs - s_lastByteUs > 300000) burstUs = firstUs;          // a quiet gap: a new burst (a new second)
        s_lastByteUs = nowUs;
        for (int i = 0; i < r; i++) {
            if (!s_nmea.feed((char)b[i])) continue;
            const GpsFix& f = s_nmea.fix;
            s_quality = f.quality;
            s_sats = f.sats;
            if (!s_nmea.lastWasRmc()) continue;
            s_timeOk = f.timeValid;
            if (!f.timeValid) continue;
            int64_t t = esp_timer_get_time();
            int64_t pps = s_ppsUs;
            bool havePps = s_pps && pps > 0 && t - pps < 1100000;
            clk::Source src = havePps ? clk::SRC_GPS_PPS : clk::SRC_GPS;
            int64_t utc = f.utcMs + (t - (havePps ? pps : burstUs)) / 1000;
            if (clk::mayOverride(src)) {
                int64_t diff = utc - clk::utcMs();
                bool better = clk::source() < src;
                if (better || llabs(diff) > (havePps ? 20 : 100) || t - lastSetUs > 600000000LL) {
                    clk::setUtcMs(utc, src);
                    lastSetUs = t;
                    ESP_LOGI(TAG, "clock set from %s (was %lld ms off)", clk::sourceName(src), (long long)diff);
                }
            }
            if (f.posValid && t - lastGridUs > 60000000LL) {
                lastGridUs = t;
                s_grid = maidenhead(f.lat, f.lon);
                s_app->gpsGrid(s_grid);
            }
        }
    }
}

void gps_start(App& app) {
    s_app = &app;
    const Settings& c = app.cfg;
    if (c.gpsRx < 0) return;
    s_baud = c.gpsBaud;
    uart_config_t u = {};
    u.baud_rate = c.gpsBaud;
    u.data_bits = UART_DATA_8_BITS;
    u.parity = UART_PARITY_DISABLE;
    u.stop_bits = UART_STOP_BITS_1;
    u.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    u.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(UART_NUM_1, 1024, 0, 0, nullptr, 0) != ESP_OK || uart_param_config(UART_NUM_1, &u) != ESP_OK ||
        uart_set_pin(UART_NUM_1, c.gpsTx >= 0 ? c.gpsTx : UART_PIN_NO_CHANGE, c.gpsRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "GPS UART did not start (RX %d, TX %d)", c.gpsRx, c.gpsTx);
        return;
    }
    if (c.gpsPps >= 0) {
        gpio_config_t g = {};
        g.pin_bit_mask = 1ULL << c.gpsPps;
        g.mode = GPIO_MODE_INPUT;
        g.intr_type = GPIO_INTR_POSEDGE;
        gpio_config(&g);
        gpio_install_isr_service(0);                            // already there (radio): harmless
        gpio_isr_handler_add((gpio_num_t)c.gpsPps, ppsIsr, nullptr);
        s_pps = true;
    }
    xTaskCreate(gpsTask, "gps", 4096, nullptr, 5, nullptr);
    ESP_LOGI(TAG, "GPS on RX %d, TX %d, %d baud%s", c.gpsRx, c.gpsTx, c.gpsBaud, s_pps ? ", PPS" : "");
}

std::string gps_status() {
    if (!s_app || s_app->cfg.gpsRx < 0) return "off";
    if (esp_timer_get_time() - s_lastByteUs > 3000000) return "no data from the module (check wiring and baud rate)";
    char b[96];
    if (!s_timeOk) snprintf(b, sizeof b, "no fix yet, %d satellites", (int)s_sats);
    else snprintf(b, sizeof b, "fix, %d satellites%s%s%s", (int)s_sats, s_pps ? ", PPS" : "", s_grid.empty() ? "" : ", ",
                  s_grid.c_str());
    return b;
}
