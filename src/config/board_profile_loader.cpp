#include "config/board_profile_loader.h"

#include "board.h"

#include <ArduinoJson.h>
#include <string.h>

namespace canshift::boards {
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
    {"cst816s", TouchDriver::CST816S},
};
constexpr EnumSlug<CanController> kCanControllers[] = {
    {"none", CanController::None},
    {"esp_twai", CanController::EspTwai},
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
    return r.ok;
}

bool parseConn(JsonObjectConst o, ConnectivityProfile &d) {
    FieldReader r{o};
    r.boolean("wifi_supported", d.wifi_supported);
    r.boolean("ble_supported", d.ble_supported);
    r.boolean("psram_present", d.psram_present);
    return r.ok;
}

BoardProfile s_runtime = kActiveBoard;
char s_boardId[kBoardIdCapacity];
char s_boardName[kBoardNameCapacity];

} // namespace

BoardProfileParse parseBoardProfileBlob(const char *json, size_t len, BoardProfile &out,
                                        char *idBuf, size_t idCap, char *nameBuf, size_t nameCap) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) {
        return BoardProfileParse::InvalidJson;
    }
    if (!doc.is<JsonObjectConst>()) {
        return BoardProfileParse::NotAnObject;
    }
    JsonObjectConst root = doc.as<JsonObjectConst>();

    JsonVariantConst magic = root["magic"];
    if (!magic.is<const char *>() || strcmp(magic.as<const char *>(), kBoardBlobMagic) != 0) {
        return BoardProfileParse::WrongMagic;
    }

    JsonVariantConst version = root["formatVersion"];
    if (!version.is<uint32_t>()) {
        return BoardProfileParse::WrongShape;
    }
    const uint32_t fmt = version.as<uint32_t>();
    if (fmt > kBoardBlobFormatVersion) {
        return BoardProfileParse::UnsupportedVersion;
    }
    if (fmt != kBoardBlobFormatVersion) {
        return BoardProfileParse::WrongShape;
    }

    JsonObjectConst p;
    if (!subObject(root, "profile", p)) {
        return BoardProfileParse::WrongShape;
    }

    if (!readString(p["board_id"], idBuf, idCap) ||
        !readString(p["board_name"], nameBuf, nameCap)) {
        return BoardProfileParse::WrongShape;
    }
    out.board_id = idBuf;
    out.board_name = nameBuf;

    {
        FieldReader r{p};
        r.enumField("chip_family", kChipFamilies, out.chip_family);
        if (!r.ok) {
            return BoardProfileParse::WrongShape;
        }
    }

    JsonObjectConst lcd, backlight, touch, can, storage, conn;
    if (!subObject(p, "lcd", lcd) || !subObject(p, "backlight", backlight) ||
        !subObject(p, "touch", touch) || !subObject(p, "can", can) ||
        !subObject(p, "storage", storage) || !subObject(p, "conn", conn)) {
        return BoardProfileParse::WrongShape;
    }

    if (!parseLcd(lcd, out.lcd) || !parseBacklight(backlight, out.backlight) ||
        !parseTouch(touch, out.touch) || !parseCan(can, out.can) ||
        !parseStorage(storage, out.storage) || !parseConn(conn, out.conn)) {
        return BoardProfileParse::WrongShape;
    }

    return BoardProfileParse::Ok;
}

const BoardProfile &runtimeBoardProfile() {
    return s_runtime;
}

bool applyBoardProfileBlob(const char *json, size_t len) {
    BoardProfile parsed;
    char idBuf[kBoardIdCapacity];
    char nameBuf[kBoardNameCapacity];
    if (parseBoardProfileBlob(json, len, parsed, idBuf, sizeof idBuf, nameBuf, sizeof nameBuf) !=
        BoardProfileParse::Ok) {
        return false;
    }
    memcpy(s_boardId, idBuf, strlen(idBuf) + 1);
    memcpy(s_boardName, nameBuf, strlen(nameBuf) + 1);
    parsed.board_id = s_boardId;
    parsed.board_name = s_boardName;
    s_runtime = parsed;
    return true;
}

void resetRuntimeBoardProfile() {
    s_runtime = kActiveBoard;
}

} // namespace canshift::boards
