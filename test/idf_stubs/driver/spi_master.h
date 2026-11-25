#pragma once
#include "driver/uart.h"
#include <cstddef>
typedef void* spi_device_handle_t; enum { SPI2_HOST = 1, SPI3_HOST = 2, SPI_DMA_CH_AUTO = 3 };
struct spi_bus_config_t { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, max_transfer_sz; };
struct spi_device_interface_config_t { int clock_speed_hz, mode, spics_io_num, queue_size; };
struct spi_transaction_t { size_t length; const void* tx_buffer; void* rx_buffer; };
int spi_bus_initialize(int, const spi_bus_config_t*, int); int spi_bus_add_device(int, const spi_device_interface_config_t*, spi_device_handle_t*);
int spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t*);
