#pragma once

using esp_err_t = int;
#define ESP_OK 0
#define ESP_FAIL -1

inline const char *esp_err_to_name(esp_err_t) {
    return "ESP_SIM";
}
