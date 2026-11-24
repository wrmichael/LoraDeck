#pragma once
#include <cstdint>
#include <cstddef>
enum { ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT };
struct esp_partition_t { uint32_t size; };
const esp_partition_t* esp_partition_find_first(int, int, const char*); int esp_partition_erase_range(const esp_partition_t*, size_t, size_t);
