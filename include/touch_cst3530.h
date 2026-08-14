#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "diag/error_store.h"
#include "diag/logger.h"
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
            LOG_ERROR("TOUCH", "i2c port %u sda %d scl %d refused to open",
                      static_cast<unsigned>(_cfg.i2c_port), static_cast<int>(_cfg.pin_sda),
                      static_cast<int>(_cfg.pin_scl));
            ErrorStore::push(ERROR_SRC_SYSTEM, "TOUCH_BUS", "touch i2c bus did not open");
            return false;
        }
        _sleeping = false;
        if (probeChip()) {
            return true;
        }
        logProbeFailure();
        return false;
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
        if (_sleeping || tp == nullptr || count == 0) {
            return 0;
        }
        if (!_inited && !reprobeDue()) {
            return 0;
        }
        uint8_t frame[cst3530::kFrameBytes] = {};
        if (!readCommand(kReadCommand, frame, sizeof(frame))) {
            noteReadFailure();
            return 0;
        }
        _readFailures = 0;
        const uint_fast8_t reported = decodePoints(frame, tp, count);
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
    static constexpr size_t kInfoChipIdByte = 0;
    static constexpr size_t kInfoMagicByte = 2;
    static constexpr size_t kInfoKeyCountByte = 27;
    static constexpr size_t kInfoResolutionXByte = 28;
    static constexpr size_t kInfoResolutionYByte = 30;
    static constexpr size_t kInfoFirmwareByte = 32;
    static constexpr uint8_t kInfoMagic = 0xCA;
    static constexpr uint8_t kProbeAttempts = 5;
    static constexpr uint8_t kFailuresBeforeReprobe = 3;
    static constexpr uint32_t kProbeRetryMs = 10;
    static constexpr uint32_t kReprobeIntervalMs = 2000;
    static constexpr uint32_t kSampleLogIntervalMs = 500;
    static constexpr uint32_t kResetHoldMs = 30;
    static constexpr uint32_t kResetReleaseMs = 50;

    bool _inited = false;
    bool _sleeping = false;
    uint8_t _readFailures = 0;
    uint32_t _lastProbeMs = 0;
    uint32_t _lastSampleLogMs = 0;

    void noteReadFailure() {
        if (_readFailures < kFailuresBeforeReprobe) {
            ++_readFailures;
            return;
        }
        _readFailures = 0;
        _inited = false;
        _lastProbeMs = lgfx::millis();
        LOG_WARN("TOUCH", "CST3530 stopped answering, re-probing every %u ms",
                 static_cast<unsigned>(kReprobeIntervalMs));
    }

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
        resetPanel();
        for (uint8_t attempt = 0; attempt < kProbeAttempts; ++attempt) {
            if (readChipInfo()) {
                return true;
            }
            lgfx::delay(kProbeRetryMs);
        }
        _lastProbeMs = lgfx::millis();
        return false;
    }

    bool reprobeDue() {
        if (lgfx::millis() - _lastProbeMs < kReprobeIntervalMs) {
            return false;
        }
        _lastProbeMs = lgfx::millis();
        return readChipInfo();
    }

    bool readChipInfo() {
        uint8_t info[kInfoBytes] = {};
        if (!readCommand(kInfoCommand, info, sizeof(info))) {
            return false;
        }
        if (info[kInfoMagicByte] != kInfoMagic || info[kInfoMagicByte + 1] != kInfoMagic) {
            return false;
        }
        logChipInfo(info);
        _inited = true;
        _readFailures = 0;
        return true;
    }

    void logProbeFailure() {
        uint8_t info[kInfoBytes] = {};
        const bool acked = readCommand(kInfoCommand, info, sizeof(info));
        LOG_ERROR("TOUCH", "CST3530 silent at 0x%02X (sda %d scl %d): %s, head %02X %02X %02X %02X",
                  static_cast<unsigned>(_cfg.i2c_addr), static_cast<int>(_cfg.pin_sda),
                  static_cast<int>(_cfg.pin_scl), acked ? "acked" : "no ack",
                  static_cast<unsigned>(info[0]), static_cast<unsigned>(info[1]),
                  static_cast<unsigned>(info[2]), static_cast<unsigned>(info[3]));
        ErrorStore::push(ERROR_SRC_SYSTEM, "TOUCH_INIT", "touch controller did not answer");
    }

    static void logChipInfo(const uint8_t *info) {
        LOG_INFO("TOUCH", "CST3530 id 0x%08X %ux%u keys %u fw 0x%08X",
                 static_cast<unsigned>(readLe32(&info[kInfoChipIdByte])),
                 static_cast<unsigned>(readLe16(&info[kInfoResolutionXByte])),
                 static_cast<unsigned>(readLe16(&info[kInfoResolutionYByte])),
                 static_cast<unsigned>(info[kInfoKeyCountByte]),
                 static_cast<unsigned>(readLe32(&info[kInfoFirmwareByte])));
    }

    static uint16_t readLe16(const uint8_t *bytes) {
        return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    }

    static uint32_t readLe32(const uint8_t *bytes) {
        return static_cast<uint32_t>(readLe16(bytes)) |
               (static_cast<uint32_t>(readLe16(bytes + 2)) << 16);
    }

    bool enterCommandMode() {
        if (!writeCommand(kCommandModeStage)) {
            return false;
        }
        lgfx::delay(1);
        return writeCommand(kCommandModeStage);
    }

    void logSampleOccasionally(const cst3530::Sample &sample, uint8_t decoded) {
        const uint32_t now = lgfx::millis();
        if (now - _lastSampleLogMs < kSampleLogIntervalMs) {
            return;
        }
        _lastSampleLogMs = now;
        LOG_VERBOSE("TOUCH", "%u pt raw %u,%u", static_cast<unsigned>(decoded),
                    static_cast<unsigned>(sample.x), static_cast<unsigned>(sample.y));
    }

    uint_fast8_t decodePoints(const uint8_t *frame, lgfx::touch_point_t *tp, uint_fast8_t count) {
        cst3530::Sample samples[cst3530::kMaxPoints] = {};
        const uint8_t capacity =
            count < cst3530::kMaxPoints ? static_cast<uint8_t>(count) : cst3530::kMaxPoints;
        const uint8_t decoded = cst3530::decodeFrame(frame, samples, capacity);
        if (decoded > 0) {
            logSampleOccasionally(samples[0], decoded);
        }
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
