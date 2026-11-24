#pragma once
#include <cstdint>
#include "driver/uart.h"
#include "wear_levelling.h"
struct esp_vfs_fat_mount_config_t { bool format_if_mount_failed; int max_files; int allocation_unit_size; };
esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(const char*, const char*, const esp_vfs_fat_mount_config_t*, wl_handle_t*);
esp_err_t esp_vfs_fat_spiflash_unmount_rw_wl(const char*, wl_handle_t);
esp_err_t esp_vfs_fat_info(const char*, uint64_t*, uint64_t*);
