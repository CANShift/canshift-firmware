#pragma once

#include "esp_err.h"
#include "esp_partition.h"

#include <cstddef>
#include <cstdint>

using esp_ota_handle_t = uint32_t;

inline constexpr size_t OTA_WITH_SEQUENTIAL_WRITES = 0xfffffffe;
inline constexpr uint32_t kBenchOtaSlotBytes = 0x180000;
inline constexpr esp_ota_handle_t kBenchOtaHandle = 1;

inline const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) {
    static const esp_partition_t slot{kBenchOtaSlotBytes, "app1"};
    return &slot;
}

inline esp_err_t esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *handle) {
    *handle = kBenchOtaHandle;
    return ESP_OK;
}

inline esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t) {
    return ESP_OK;
}

inline esp_err_t esp_ota_end(esp_ota_handle_t) {
    return ESP_OK;
}

inline esp_err_t esp_ota_abort(esp_ota_handle_t) {
    return ESP_OK;
}

inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t *) {
    return ESP_OK;
}
