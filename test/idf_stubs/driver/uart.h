#pragma once
#include <cstdint>
typedef int esp_err_t; enum { ESP_OK = 0 };
typedef enum { UART_NUM_1 = 1 } uart_port_t;
enum { UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE, UART_SCLK_DEFAULT };
#define UART_PIN_NO_CHANGE (-1)
struct uart_config_t { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; };
esp_err_t uart_driver_install(uart_port_t, int, int, int, void*, int);
esp_err_t uart_param_config(uart_port_t, const uart_config_t*);
esp_err_t uart_set_pin(uart_port_t, int, int, int, int);
int uart_read_bytes(uart_port_t, void*, uint32_t, int);
