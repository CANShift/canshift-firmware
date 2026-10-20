#include "config/board_profile_fields.h"

#include <string.h>

namespace canshift::boards {

bool readString(JsonVariantConst v, char *buf, size_t cap) {
    if (!v.is<const char *>()) {
        return false;
    }
    const char *s = v.as<const char *>();
    if (s == nullptr) {
        return false;
    }
    const size_t n = strlen(s);
    if (n + 1 > cap) {
        return false;
    }
    memcpy(buf, s, n + 1);
    return true;
}

bool subObject(JsonObjectConst parent, const char *key, JsonObjectConst &out) {
    JsonVariantConst v = parent[key];
    if (!v.is<JsonObjectConst>()) {
        return false;
    }
    out = v.as<JsonObjectConst>();
    return true;
}

namespace {

template <typename E>
struct EnumSlug {
    const char *slug;
    E value;
};

constexpr EnumSlug<ChipFamily> kChipFamilies[] = {
    {"esp32", ChipFamily::Esp32},
    {"esp32s3", ChipFamily::Esp32s3},
};
constexpr EnumSlug<LcdDriver> kLcdDrivers[] = {
    {"ili9341", LcdDriver::ILI9341},
    {"st7789", LcdDriver::ST7789},
    {"ili9488", LcdDriver::ILI9488},
    {"gc9a01", LcdDriver::GC9A01},
};
constexpr EnumSlug<TouchDriver> kTouchDrivers[] = {
    {"none", TouchDriver::None},       {"xpt2046", TouchDriver::XPT2046},
    {"ft6336", TouchDriver::FT6336},   {"gt911", TouchDriver::GT911},
    {"cst816s", TouchDriver::CST816S}, {"cst3530", TouchDriver::CST3530},
};
constexpr EnumSlug<CanController> kCanControllers[] = {
    {"none", CanController::None},
    {"esp_twai", CanController::EspTwai},
};
constexpr EnumSlug<SdBus> kSdBuses[] = {
    {"none", SdBus::None},
    {"sdmmc_1bit", SdBus::Sdmmc1Bit},
};
constexpr EnumSlug<ImuDriver> kImuDrivers[] = {
    {"none", ImuDriver::None},
    {"qmi8658", ImuDriver::QMI8658},
};
constexpr EnumSlug<ImuAxis> kImuAxes[] = {
    {"x", ImuAxis::PosX},  {"-x", ImuAxis::NegX}, {"y", ImuAxis::PosY},
    {"-y", ImuAxis::NegY}, {"z", ImuAxis::PosZ},  {"-z", ImuAxis::NegZ},
};

template <typename E, size_t N>
bool matchEnum(const EnumSlug<E> (&table)[N], const char *slug, E &out) {
    if (slug == nullptr) {
        return false;
    }
    for (const auto &entry : table) {
        if (strcmp(entry.slug, slug) == 0) {
            out = entry.value;
            return true;
        }
    }
    return false;
}

struct FieldReader {
    explicit FieldReader(JsonObjectConst object) : obj(object) {}

    JsonObjectConst obj;
    bool ok = true;

    void boolean(const char *k, bool &d) {
        if (ok && !readTyped<bool>(k, d)) {
            ok = false;
        }
    }
    void i8(const char *k, int8_t &d) {
        if (ok && !readTyped<int8_t>(k, d)) {
            ok = false;
        }
    }
    void optionalI8(const char *k, int8_t &d, int8_t fallback) {
        d = fallback;
        if (!obj[k].isNull()) {
            i8(k, d);
        }
    }
    void u8(const char *k, uint8_t &d) {
        if (ok && !readTyped<uint8_t>(k, d)) {
            ok = false;
        }
    }
    void u16(const char *k, uint16_t &d) {
        if (ok && !readTyped<uint16_t>(k, d)) {
            ok = false;
        }
    }
    void u32(const char *k, uint32_t &d) {
        if (ok && !readTyped<uint32_t>(k, d)) {
            ok = false;
        }
    }
    template <typename E, size_t N>
    void enumField(const char *k, const EnumSlug<E> (&table)[N], E &d) {
        if (!ok) {
            return;
        }
        JsonVariantConst v = obj[k];
        if (!v.is<const char *>() || !matchEnum(table, v.as<const char *>(), d)) {
            ok = false;
        }
    }
    template <typename E, size_t N>
    void optionalEnum(const char *k, const EnumSlug<E> (&table)[N], E &d, E fallback) {
        d = fallback;
        if (!obj[k].isNull()) {
            enumField(k, table, d);
        }
    }

  private:
    template <typename T>
    bool readTyped(const char *k, T &d) {
        JsonVariantConst v = obj[k];
        if (!v.is<T>()) {
            return false;
        }
        d = v.as<T>();
        return true;
    }
};

bool parseLcd(JsonObjectConst o, LcdProfile &d) {
    FieldReader r{o};
    r.enumField("driver", kLcdDrivers, d.driver);
    r.i8("pin_mosi", d.pin_mosi);
    r.i8("pin_miso", d.pin_miso);
    r.i8("pin_sclk", d.pin_sclk);
    r.i8("pin_cs", d.pin_cs);
    r.i8("pin_dc", d.pin_dc);
    r.i8("pin_rst", d.pin_rst);
    r.i8("pin_bl", d.pin_bl);
    r.u32("freq_write_hz", d.freq_write_hz);
    r.u16("panel_width", d.panel_width);
    r.u16("panel_height", d.panel_height);
    r.u16("memory_width", d.memory_width);
    r.u16("memory_height", d.memory_height);
    r.u8("default_rotation", d.default_rotation);
    r.boolean("rgb_order_bgr", d.rgb_order_bgr);
    r.boolean("invert", d.invert);
    r.boolean("bus_shared_with_touch", d.bus_shared_with_touch);
    r.boolean("readable", d.readable);
    r.u8("color_depth", d.color_depth);
    return r.ok;
}

bool parseBacklight(JsonObjectConst o, BacklightProfile &d) {
    FieldReader r{o};
    r.boolean("present", d.present);
    r.u8("pwm_channel", d.pwm_channel);
    r.u32("pwm_freq_hz", d.pwm_freq_hz);
    r.u8("default_duty", d.default_duty);
    r.boolean("invert", d.invert);
    return r.ok;
}

bool parseTouch(JsonObjectConst o, TouchProfile &d) {
    FieldReader r{o};
    r.enumField("driver", kTouchDrivers, d.driver);
    r.i8("pin_cs", d.pin_cs);
    r.i8("pin_irq", d.pin_irq);
    r.u32("freq_hz", d.freq_hz);
    r.boolean("needs_calibration", d.needs_calibration);
    r.i8("pin_sda", d.pin_sda);
    r.i8("pin_scl", d.pin_scl);
    r.optionalI8("pin_rst", d.pin_rst, -1);
    return r.ok;
}

bool parseCan(JsonObjectConst o, CanProfile &d) {
    FieldReader r{o};
    r.enumField("controller", kCanControllers, d.controller);
    r.i8("pin_tx", d.pin_tx);
    r.i8("pin_rx", d.pin_rx);
    r.u16("default_speed_kbps", d.default_speed_kbps);
    return r.ok;
}

bool parseStorage(JsonObjectConst o, StorageProfile &d) {
    FieldReader r{o};
    r.boolean("spiffs_present", d.spiffs_present);
    r.u16("spiffs_size_kb", d.spiffs_size_kb);
    r.boolean("sd_present", d.sd_present);
    r.i8("sd_pin_cs", d.sd_pin_cs);
    r.optionalEnum("sd_bus", kSdBuses, d.sd_bus, SdBus::None);
    r.optionalI8("sd_pin_clk", d.sd_pin_clk, -1);
    r.optionalI8("sd_pin_cmd", d.sd_pin_cmd, -1);
    r.optionalI8("sd_pin_d0", d.sd_pin_d0, -1);
    r.optionalI8("sd_pin_d3", d.sd_pin_d3, -1);
    return r.ok;
}

bool parseConn(JsonObjectConst o, ConnectivityProfile &d) {
    FieldReader r{o};
    r.boolean("wifi_supported", d.wifi_supported);
    r.boolean("ble_supported", d.ble_supported);
    r.boolean("psram_present", d.psram_present);
    return r.ok;
}

uint8_t axisIndex(ImuAxis axis) {
    return static_cast<uint8_t>(axis) / 2;
}

bool axesDistinct(const ImuProfile &d) {
    const uint8_t x = axisIndex(d.screen_x);
    const uint8_t y = axisIndex(d.screen_y);
    const uint8_t z = axisIndex(d.screen_z);
    return x != y && y != z && x != z;
}

bool parseImuFields(JsonObjectConst o, ImuProfile &d) {
    FieldReader r{o};
    r.enumField("driver", kImuDrivers, d.driver);
    r.i8("pin_sda", d.pin_sda);
    r.i8("pin_scl", d.pin_scl);
    r.u8("i2c_addr", d.i2c_addr);
    r.u32("freq_hz", d.freq_hz);
    r.enumField("screen_x", kImuAxes, d.screen_x);
    r.enumField("screen_y", kImuAxes, d.screen_y);
    r.enumField("screen_z", kImuAxes, d.screen_z);
    return r.ok && axesDistinct(d);
}

bool parseImu(JsonObjectConst profile, ImuProfile &d) {
    d = kNoImu;
    if (profile["imu"].isNull()) {
        return true;
    }
    JsonObjectConst o;
    return subObject(profile, "imu", o) && parseImuFields(o, d);
}

struct ProfileSections {
    JsonObjectConst lcd;
    JsonObjectConst backlight;
    JsonObjectConst touch;
    JsonObjectConst can;
    JsonObjectConst storage;
    JsonObjectConst conn;
};

bool findSections(JsonObjectConst p, ProfileSections &s) {
    return subObject(p, "lcd", s.lcd) && subObject(p, "backlight", s.backlight) &&
           subObject(p, "touch", s.touch) && subObject(p, "can", s.can) &&
           subObject(p, "storage", s.storage) && subObject(p, "conn", s.conn);
}

bool parseSections(const ProfileSections &s, BoardProfile &out) {
    return parseLcd(s.lcd, out.lcd) && parseBacklight(s.backlight, out.backlight) &&
           parseTouch(s.touch, out.touch) && parseCan(s.can, out.can) &&
           parseStorage(s.storage, out.storage) && parseConn(s.conn, out.conn);
}

}

bool parseChipFamily(JsonVariantConst v, ChipFamily &out) {
    return v.is<const char *>() && matchEnum(kChipFamilies, v.as<const char *>(), out);
}

bool parseProfileSections(JsonObjectConst profile, BoardProfile &out) {
    ProfileSections sections;
    return findSections(profile, sections) && parseSections(sections, out) &&
           parseImu(profile, out.imu);
}

}
