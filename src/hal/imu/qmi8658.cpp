#include "hal/imu/qmi8658.h"

#include "diag/error_store.h"
#include "diag/logger.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace {

using canshift::boards::ImuProfile;

constexpr int kI2cPort = 1;

constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kRegCtrl2 = 0x03;
constexpr uint8_t kRegCtrl7 = 0x08;
constexpr uint8_t kRegAccelXLow = 0x35;

constexpr uint8_t kWhoAmIValue = 0x05;
constexpr uint8_t kCtrl1AutoIncrement = 0x40;
constexpr uint8_t kCtrl2Accel4gAt125Hz = 0x16;
constexpr uint8_t kCtrl7AccelOnly = 0x01;
constexpr float kLsbPerG = 8192.0f;
constexpr size_t kAccelBytes = 6;

struct RegisterWrite {
    uint8_t reg;
    uint8_t value;
};

constexpr RegisterWrite kConfiguration[] = {
    {kRegCtrl1, kCtrl1AutoIncrement},
    {kRegCtrl2, kCtrl2Accel4gAt125Hz},
    {kRegCtrl7, kCtrl7AccelOnly},
};

bool writeRegister(const ImuProfile &imu, const RegisterWrite &write) {
    return lgfx::i2c::writeRegister8(kI2cPort, imu.i2c_addr, write.reg, write.value, 0, imu.freq_hz)
        .has_value();
}

bool probe(const ImuProfile &imu) {
    const auto who = lgfx::i2c::readRegister8(kI2cPort, imu.i2c_addr, kRegWhoAmI, imu.freq_hz);
    if (who.has_value() && who.value() == kWhoAmIValue)
        return true;
    LOG_ERROR("IMU", "QMI8658 silent at 0x%02X (sda %d scl %d): %s",
              static_cast<unsigned>(imu.i2c_addr), static_cast<int>(imu.pin_sda),
              static_cast<int>(imu.pin_scl), who.has_value() ? "wrong WHO_AM_I" : "no ack");
    ErrorStore::push(ERROR_SRC_SYSTEM, "IMU_INIT", "imu did not answer");
    return false;
}

bool configure(const ImuProfile &imu) {
    for (const RegisterWrite &write : kConfiguration) {
        if (writeRegister(imu, write))
            continue;
        LOG_ERROR("IMU", "QMI8658 refused register 0x%02X", static_cast<unsigned>(write.reg));
        ErrorStore::push(ERROR_SRC_SYSTEM, "IMU_INIT", "imu configuration failed");
        return false;
    }
    return true;
}

float axisG(const uint8_t *bytes) {
    const int16_t raw = static_cast<int16_t>(bytes[0] | (bytes[1] << 8));
    return static_cast<float>(raw) / kLsbPerG;
}

}

namespace Qmi8658 {

bool begin(const ImuProfile &imu) {
    if (!lgfx::i2c::init(kI2cPort, imu.pin_sda, imu.pin_scl).has_value()) {
        LOG_ERROR("IMU", "i2c port %d sda %d scl %d refused to open", kI2cPort,
                  static_cast<int>(imu.pin_sda), static_cast<int>(imu.pin_scl));
        ErrorStore::push(ERROR_SRC_SYSTEM, "IMU_BUS", "imu i2c bus did not open");
        return false;
    }
    return probe(imu) && configure(imu);
}

bool readAccelG(const ImuProfile &imu, float (&out)[3]) {
    uint8_t bytes[kAccelBytes] = {};
    if (!lgfx::i2c::readRegister(kI2cPort, imu.i2c_addr, kRegAccelXLow, bytes, sizeof(bytes),
                                 imu.freq_hz)
             .has_value())
        return false;
    for (size_t axis = 0; axis < 3; ++axis)
        out[axis] = axisG(&bytes[axis * 2]);
    return true;
}

}
