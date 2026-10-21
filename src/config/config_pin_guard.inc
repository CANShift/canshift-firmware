#include "config_loader_internal.h"

#include "config/board_profile_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <cstdint>

namespace ConfigLoaderInternal {

namespace {

using canshift::boards::BoardProfile;

constexpr int kGpioBits = 64;

constexpr uint64_t pinRange(int first, int last) {
    return ((1ULL << (last + 1)) - 1) & ~((1ULL << first) - 1);
}

struct ChipPins {
    uint64_t input;
    uint64_t output;
};

constexpr uint64_t kEsp32Gpio = pinRange(0, 39) & ~(pinRange(24, 24) | pinRange(28, 31));
constexpr uint64_t kEsp32Reserved = pinRange(1, 1) | pinRange(3, 3) | pinRange(6, 11);
constexpr uint64_t kEsp32Input = kEsp32Gpio & ~kEsp32Reserved;
constexpr uint64_t kEsp32Output = kEsp32Input & ~pinRange(34, 39);
constexpr uint64_t kEsp32s3Gpio = pinRange(0, 21) | pinRange(26, 48);
constexpr uint64_t kEsp32s3Reserved = pinRange(19, 20) | pinRange(26, 37);
constexpr uint64_t kEsp32s3Usable = kEsp32s3Gpio & ~kEsp32s3Reserved;

constexpr ChipPins kChipPins[] = {
    {kEsp32Input, kEsp32Output},
    {kEsp32s3Usable, kEsp32s3Usable},
};

static_assert(sizeof(kChipPins) / sizeof(kChipPins[0]) ==
                  static_cast<size_t>(canshift::boards::ChipFamily::Esp32s3) + 1,
              "every chip family needs a pin map");

bool chipAllowsPin(const BoardProfile &board, int pin, PinUse use) {
    const ChipPins &chip = kChipPins[static_cast<size_t>(board.chip_family)];
    const uint64_t usable = use == PinUse::Output ? chip.output : chip.input;
    return (usable >> pin) & 1ULL;
}

bool boardOwnsPin(const BoardProfile &b, int pin) {
    const int8_t owned[] = {
        b.lcd.pin_mosi,      b.lcd.pin_miso,       b.lcd.pin_sclk,       b.lcd.pin_cs,
        b.lcd.pin_dc,        b.lcd.pin_rst,        b.lcd.pin_bl,         b.touch.pin_cs,
        b.touch.pin_irq,     b.touch.pin_sda,      b.touch.pin_scl,      b.touch.pin_rst,
        b.storage.sd_pin_cs, b.storage.sd_pin_clk, b.storage.sd_pin_cmd, b.storage.sd_pin_d0,
        b.storage.sd_pin_d3, b.audio.pin_bck,      b.audio.pin_ws,       b.audio.pin_dout,
        b.imu.pin_sda,       b.imu.pin_scl,
    };
    for (const int8_t used : owned) {
        if (used == pin)
            return true;
    }
    return false;
}

}

bool isUsablePin(int pin, PinUse use) {
    if (pin < 0 || pin >= kGpioBits)
        return false;
    const BoardProfile &board = canshift::boards::runtimeBoardProfile();
    return chipAllowsPin(board, pin, use) && !boardOwnsPin(board, pin);
}

int checkedTwaiPin(const char *key, int pin, int8_t boardPin, PinUse use) {
    if (pin < 0 || pin == boardPin || isUsablePin(pin, use))
        return pin;
    LOG_WARN("CFG", "device.json %s=%d is not usable on this board — GPIO%d kept", key, pin,
             boardPin);
    ErrorStore::push(ERROR_SRC_CONFIG, "PIN_REJECT", key);
    return -1;
}

}
