#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "touch_cst3530_frame.h"

namespace canshift::touch {

class Touch_CST3530 : public lgfx::ITouch {
  public:
    Touch_CST3530() {
        _cfg.i2c_addr = kI2cAddr;
        _cfg.x_min = 0;
        _cfg.x_max = kPanelWidth - 1;
        _cfg.y_min = 0;
        _cfg.y_max = kPanelHeight - 1;
    }

    bool init() override {
        if (_inited) {
            return true;
        }
        if (!lgfx::i2c::init(_cfg.i2c_port, _cfg.pin_sda, _cfg.pin_scl).has_value()) {
            return false;
        }
        resetPanel();
        _inited = probeChip();
        _sleeping = false;
        return _inited;
    }

    void wakeup() override {
        resetPanel();
        if (!enterCommandMode()) {
            return;
        }
        _sleeping =
            !(writeCommand(kWakeStage1) && writeCommand(kWakeStage2) && writeCommand(kWakeStage3));
    }

    void sleep() override {
        if (!enterCommandMode()) {
            return;
        }
        _sleeping = writeCommand(kSleepCommand);
    }

    uint_fast8_t getTouchRaw(lgfx::touch_point_t *tp, uint_fast8_t count) override {
        if (!_inited || _sleeping || tp == nullptr || count == 0) {
            return 0;
        }
        uint8_t frame[cst3530::kFrameBytes] = {};
        const uint_fast8_t reported =
            readCommand(kReadCommand, frame, sizeof(frame)) ? decodePoints(frame, tp, count) : 0;
        if (!writeCommand(kClearCommand)) {
            return 0;
        }
        return reported;
    }

  private:
    static constexpr int16_t kI2cAddr = 0x58;
    static constexpr uint16_t kPanelWidth = 240;
    static constexpr uint16_t kPanelHeight = 320;
    static constexpr uint32_t kReadCommand = 0xD0070000;
    static constexpr uint32_t kInfoCommand = 0xD0030000;
    static constexpr uint32_t kSleepCommand = 0xD00022AB;
    static constexpr uint32_t kClearCommand = 0xD00002AB;
    static constexpr uint32_t kCommandModeStage = 0xD0000400;
    static constexpr uint32_t kWakeStage1 = 0xD0000000;
    static constexpr uint32_t kWakeStage2 = 0xD0000C00;
    static constexpr uint32_t kWakeStage3 = 0xD0000100;
    static constexpr size_t kCommandBytes = 4;
    static constexpr size_t kInfoBytes = 50;
    static constexpr size_t kInfoMagicByte = 2;
    static constexpr uint8_t kInfoMagic = 0xCA;
    static constexpr uint8_t kProbeAttempts = 5;
    static constexpr uint32_t kProbeRetryMs = 10;
    static constexpr uint32_t kResetHoldMs = 30;
    static constexpr uint32_t kResetReleaseMs = 50;

    bool _inited = false;
    bool _sleeping = false;

    void resetPanel() {
        if (_cfg.pin_rst < 0) {
            return;
        }
        lgfx::pinMode(_cfg.pin_rst, lgfx::pin_mode_t::output);
        lgfx::gpio_hi(_cfg.pin_rst);
        lgfx::delay(kResetHoldMs);
        lgfx::gpio_lo(_cfg.pin_rst);
        lgfx::delay(kResetHoldMs);
        lgfx::gpio_hi(_cfg.pin_rst);
        lgfx::delay(kResetReleaseMs);
    }

    bool probeChip() {
        for (uint8_t attempt = 0; attempt < kProbeAttempts; ++attempt) {
            uint8_t info[kInfoBytes] = {};
            if (readCommand(kInfoCommand, info, sizeof(info)) &&
                info[kInfoMagicByte] == kInfoMagic && info[kInfoMagicByte + 1] == kInfoMagic) {
                return true;
            }
            lgfx::delay(kProbeRetryMs);
        }
        return false;
    }

    bool enterCommandMode() {
        if (!writeCommand(kCommandModeStage)) {
            return false;
        }
        lgfx::delay(1);
        return writeCommand(kCommandModeStage);
    }

    static uint_fast8_t decodePoints(const uint8_t *frame, lgfx::touch_point_t *tp,
                                     uint_fast8_t count) {
        cst3530::Sample samples[cst3530::kMaxPoints] = {};
        const uint8_t capacity =
            count < cst3530::kMaxPoints ? static_cast<uint8_t>(count) : cst3530::kMaxPoints;
        const uint8_t decoded = cst3530::decodeFrame(frame, samples, capacity);
        for (uint8_t i = 0; i < decoded; ++i) {
            tp[i].x = static_cast<int16_t>(samples[i].x);
            tp[i].y = static_cast<int16_t>(samples[i].y);
            tp[i].size = samples[i].pressure;
            tp[i].id = samples[i].id;
        }
        return decoded;
    }

    static void packCommand(uint32_t command, uint8_t (&out)[kCommandBytes]) {
        out[0] = static_cast<uint8_t>(command >> 24);
        out[1] = static_cast<uint8_t>(command >> 16);
        out[2] = static_cast<uint8_t>(command >> 8);
        out[3] = static_cast<uint8_t>(command);
    }

    bool writeCommand(uint32_t command) {
        uint8_t bytes[kCommandBytes] = {};
        packCommand(command, bytes);
        return lgfx::i2c::transactionWrite(_cfg.i2c_port, _cfg.i2c_addr, bytes, sizeof(bytes),
                                           _cfg.freq)
            .has_value();
    }

    bool readCommand(uint32_t command, uint8_t *out, size_t length) {
        uint8_t bytes[kCommandBytes] = {};
        packCommand(command, bytes);
        return lgfx::i2c::transactionWriteRead(_cfg.i2c_port, _cfg.i2c_addr, bytes, sizeof(bytes),
                                               out, length, _cfg.freq)
            .has_value();
    }
};

} // namespace canshift::touch
