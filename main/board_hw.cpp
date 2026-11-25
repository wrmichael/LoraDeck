// Board-specific start-up, before the radio: on the Cardputer-Adv the Cap LoRa-1262's antenna switch is powered
// through a PI4IOE5V6408 I/O expander (I2C 0x43, pin P0); nothing to do on the Heltec V3.
#include "board.h"
#include "board_pins.h"
#include "esp_log.h"

static const char* TAG = "board";

#if defined(LORADECK_CARDPUTER)
#include "driver/i2c_master.h"

static i2c_master_bus_handle_t s_bus;
static bool s_cap = false;

static bool pi4ioWrite(i2c_master_dev_handle_t d, uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(d, b, 2, 100) == ESP_OK;
}

bool board_init() {
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
#else
bool board_init() { return true; }
#endif

const char* board_id() { return LD_BOARD_ID; }
const char* board_name() { return LD_BOARD_NAME; }
