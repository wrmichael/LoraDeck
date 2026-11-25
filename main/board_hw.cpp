// Board-specific start-up, before the radio: on the Cardputer-Adv the Cap LoRa-1262's antenna switch is powered
// through a PI4IOE5V6408 I/O expander (I2C 0x43, pin P0); nothing to do on the Heltec V3.
#include "board.h"
#include "board_pins.h"
#include "esp_log.h"

static const char* TAG = "board";

#if defined(LORADECK_CARDPUTER)
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"

// Battery: G10 (ADC1 channel 9) through a 2:1 divider, as M5Stack's own library reads it.
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_adcOk = false, s_caliOk = false;
static void batteryInit() {
    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&u, &s_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t c = {};
    c.atten = ADC_ATTEN_DB_12;
    c.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(s_adc, ADC_CHANNEL_9, &c) != ESP_OK) return;
    s_adcOk = true;
    adc_cali_curve_fitting_config_t cc = {};
    cc.unit_id = ADC_UNIT_1;
    cc.chan = ADC_CHANNEL_9;
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    s_caliOk = adc_cali_create_scheme_curve_fitting(&cc, &s_cali) == ESP_OK;
}

int board_battery_mv() {
    static int s_mv = -1;
    static int64_t s_at = -10000000;
    int64_t t = esp_timer_get_time();
    if (!s_adcOk || t - s_at < 2000000) return s_mv;       // read every 2 s at most
    s_at = t;
    int sum = 0, n = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_9, &raw) != ESP_OK) continue;
        if (s_caliOk && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) sum += mv;
        else sum += raw * 3100 / 4095;
        n++;
    }
    int v = n ? sum / n * 2 : -1;
    s_mv = (s_mv < 0 || v < 0) ? v : (s_mv * 3 + v) / 4;    // a little smoothing
    return s_mv;
}

static i2c_master_bus_handle_t s_bus;
static bool s_cap = false;

static bool pi4ioWrite(i2c_master_dev_handle_t d, uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(d, b, 2, 100) == ESP_OK;
}

bool board_init() {
    batteryInit();
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = LD_I2C_SDA;
    bc.scl_io_num = LD_I2C_SCL;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) { ESP_LOGE(TAG, "I2C bus did not start"); return false; }
    if (i2c_master_probe(s_bus, 0x43, 100) != ESP_OK) {
        ESP_LOGE(TAG, "Cap LoRa-1262 not found (no PI4IO at I2C 0x43): is the cap plugged in?");
        return false;
    }
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = 0x43;
    dc.scl_speed_hz = 400000;
    i2c_master_dev_handle_t pi4io;
    if (i2c_master_bus_add_device(s_bus, &dc, &pi4io) != ESP_OK) return false;
    // P0 output (IO direction 0x03), not high-impedance (0x07), high (output state 0x05): antenna switch on
    s_cap = pi4ioWrite(pi4io, 0x03, 0x01) && pi4ioWrite(pi4io, 0x07, 0x00) && pi4ioWrite(pi4io, 0x05, 0x01);
    if (s_cap) ESP_LOGI(TAG, "Cap LoRa-1262: antenna switch on");
    else ESP_LOGE(TAG, "Cap LoRa-1262: could not switch the antenna switch on");
    return s_cap;
}
i2c_master_bus_handle_t board_i2c_bus() { return s_bus; }   // the keyboard is on it too
#else
bool board_init() { return true; }
int board_battery_mv() { return -1; }                    // not wired up on the Heltec yet
#endif

const char* board_id() { return LD_BOARD_ID; }
const char* board_name() { return LD_BOARD_NAME; }

// Li-ion charge from its voltage (resting, roughly); -1 when there is no reading (no battery, or its switch off).
int board_battery_pct() {
    int mv = board_battery_mv();
    if (mv < 3000) return -1;
    static const int T[][2] = {{3450, 0}, {3610, 10}, {3670, 20}, {3710, 30}, {3750, 40}, {3790, 50},
                               {3840, 60}, {3900, 70}, {3970, 80}, {4050, 90}, {4150, 100}};
    if (mv <= T[0][0]) return 0;
    for (int i = 1; i < 11; i++)
        if (mv < T[i][0]) return T[i - 1][1] + (mv - T[i - 1][0]) * (T[i][1] - T[i - 1][1]) / (T[i][0] - T[i - 1][0]);
    return 100;
}
