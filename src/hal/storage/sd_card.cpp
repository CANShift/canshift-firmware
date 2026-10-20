#include "app_config.h"
#if APP_SD_DATALOG_ENABLED

    #include "sd_card.h"

    #include "config/board_profile_loader.h"
    #include "diag/logger.h"

    #include <atomic>
    #include <driver/gpio.h>
    #include <driver/sdmmc_host.h>
    #include <esp_vfs_fat.h>
    #include <sdmmc_cmd.h>

namespace SdCard {

namespace {

constexpr uint8_t kBusWidth = 1;
constexpr int kMaxOpenFiles = 2;
constexpr size_t kAllocationUnitBytes = 16U * 1024U;
constexpr uint64_t kBytesPerMb = 1024ULL * 1024ULL;

std::atomic<bool> s_mounted{false};
sdmmc_card_t *s_card = nullptr;

const canshift::boards::StorageProfile &storage() {
    return canshift::boards::runtimeBoardProfile().storage;
}

void holdCardInSdMode(int8_t pinD3) {
    if (pinD3 < 0)
        return;
    const gpio_num_t pin = static_cast<gpio_num_t>(pinD3);
    if (gpio_set_direction(pin, GPIO_MODE_OUTPUT) != ESP_OK || gpio_set_level(pin, 1) != ESP_OK)
        LOG_WARN("SD", "D3 (GPIO %d) not held high, the card may answer in SPI mode", pinD3);
}

sdmmc_slot_config_t slotConfig(const canshift::boards::StorageProfile &slot) {
    sdmmc_slot_config_t config = SDMMC_SLOT_CONFIG_DEFAULT();
    config.clk = static_cast<gpio_num_t>(slot.sd_pin_clk);
    config.cmd = static_cast<gpio_num_t>(slot.sd_pin_cmd);
    config.d0 = static_cast<gpio_num_t>(slot.sd_pin_d0);
    config.width = kBusWidth;
    config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    return config;
}

}

bool slotPresent() {
    return storage().sd_present && storage().sd_bus == canshift::boards::SdBus::Sdmmc1Bit;
}

bool mount() {
    if (s_mounted.load())
        return true;
    if (!slotPresent())
        return false;
    holdCardInSdMode(storage().sd_pin_d3);
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    const sdmmc_slot_config_t slot = slotConfig(storage());
    esp_vfs_fat_sdmmc_mount_config_t options = {};
    options.format_if_mount_failed = false;
    options.max_files = kMaxOpenFiles;
    options.allocation_unit_size = kAllocationUnitBytes;
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot, &options, &s_card);
    if (err != ESP_OK) {
        LOG_WARN("SD", "no card mounted: %s", esp_err_to_name(err));
        return false;
    }
    LOG_INFO("SD", "card mounted: %u MB",
             static_cast<unsigned>(static_cast<uint64_t>(s_card->csd.capacity) *
                                   s_card->csd.sector_size / kBytesPerMb));
    s_mounted.store(true);
    return true;
}

void unmount() {
    if (!s_mounted.exchange(false))
        return;
    const esp_err_t err = esp_vfs_fat_sdcard_unmount(kMountPoint, s_card);
    s_card = nullptr;
    if (err != ESP_OK)
        LOG_WARN("SD", "unmount failed: %s", esp_err_to_name(err));
}

bool mounted() {
    return s_mounted.load();
}

}

#endif
