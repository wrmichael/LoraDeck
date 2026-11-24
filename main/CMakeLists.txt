idf_component_register(SRCS "main.cpp" "sx1262.cpp" "oled.cpp" "net.cpp" "web.cpp" "ota.cpp" "conlog.cpp" "gps.cpp"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "web/index.html"
                       REQUIRES core esp_wifi esp_netif esp_event esp_http_server nvs_flash fatfs wear_levelling
                                esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_uart app_update esp_timer lwip esp_rom)
