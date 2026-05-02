// maxxecu_parser.cpp — MaxxECU CAN protocol parser implementation

#include "maxxecu_parser.h"
#include "signal_map.h"
#include "runtime/signal_store.h"
#include "config/config_loader.h"
#include "diag/logger.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Internal state — runtime signal dispatch table
// ---------------------------------------------------------------------------

namespace {

// Mapping from signals.json "name" string to firmware SignalId.
// Add entries here when new signals are added to signal_map.h.
struct NameEntry {
    const char *name;
    SignalId id;
};

static constexpr NameEntry kNameMap[] = {
    { "rpm",               SignalIds::RPM },
    { "throttle_pos",      SignalIds::THROTTLE_POS },
    { "map_kpa",           SignalIds::MAP_KPA },
    { "boost_bar",         SignalIds::BOOST_BAR },
    { "iat_c",             SignalIds::IAT_C },
    { "coolant_temp_c",    SignalIds::COOLANT_TEMP_C },
    { "oil_temp_c",        SignalIds::OIL_TEMP_C },
    { "oil_press_bar",     SignalIds::OIL_PRESS_BAR },
    { "fuel_press_bar",    SignalIds::FUEL_PRESS_BAR },
    { "lambda_1",          SignalIds::LAMBDA_1 },
    { "afr_1",             SignalIds::AFR_1 },
    { "speed_kph",         SignalIds::SPEED_KPH },
    { "gear",              SignalIds::GEAR },
    { "battery_volts",     SignalIds::BATTERY_VOLTS },
    { "flag_mil",          SignalIds::FLAG_MIL },
    { "flag_launch_ctrl",  SignalIds::FLAG_LAUNCH_CTRL },
    { "flag_flat_shift",   SignalIds::FLAG_FLAT_SHIFT },
    { "flag_anti_lag",     SignalIds::FLAG_ANTI_LAG },
    { "flag_traction_cut", SignalIds::FLAG_TRACTION_CUT },
    { "map_number",        SignalIds::MAP_NUMBER },
    { "map_name_idx",      SignalIds::MAP_NAME_IDX },
    { "lap_timer_ms",      SignalIds::LAP_TIMER_MS },
};
static constexpr uint8_t kNameMapLen = sizeof(kNameMap) / sizeof(kNameMap[0]);

// One entry per decoded signal — built from signals.json at runtime
struct RuntimeSignal {
    uint32_t canFrameId;
    uint8_t startByte;
    uint8_t byteLength; // 1, 2, or 4
    bool bigEndian;
    bool isSigned;
    float scale;
    float offset;
    uint8_t bitMask;   // 0 = full value; non-zero = boolean flag extracted from mask
    SignalId signalId;
};

static RuntimeSignal s_runtime[CONFIG_MAX_SIGNALS];
static uint8_t s_runtimeCount = 0;
static bool s_runtimeLoaded = false;

// ---------------------------------------------------------------------------
// Generic multi-byte decoder
// ---------------------------------------------------------------------------

float decodeBytes(const uint8_t *data, uint8_t startByte, uint8_t byteLen,
                  bool bigEndian, bool isSigned, uint8_t bitMask,
                  float scale, float offset) {
    if (byteLen == 0 || startByte + byteLen > 8)
        return 0.0f;

    uint32_t raw = 0;
    if (bigEndian) {
        for (uint8_t i = 0; i < byteLen; ++i)
            raw = (raw << 8) | data[startByte + i];
    } else {
        for (uint8_t i = 0; i < byteLen; ++i)
            raw |= static_cast<uint32_t>(data[startByte + i]) << (i * 8);
    }

    // Boolean flag: apply bitmask and return 0 or 1
    if (bitMask != 0)
        return (raw & bitMask) ? 1.0f : 0.0f;

    float physical;
    if (isSigned) {
        const uint8_t bits = byteLen * 8;
        if (raw & (1u << (bits - 1)))
            raw |= ~((1u << bits) - 1); // sign-extend to 32 bits
        physical = static_cast<float>(static_cast<int32_t>(raw));
    } else {
        physical = static_cast<float>(raw);
    }
    return physical * scale + offset;
}

// ---------------------------------------------------------------------------
// Hardcoded fallback helpers (used when runtime table is not loaded)
// ---------------------------------------------------------------------------

inline uint16_t readU16BE(const uint8_t *data, uint8_t offset) {
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

inline int16_t readI16BE(const uint8_t *data, uint8_t offset) {
    return static_cast<int16_t>((data[offset] << 8) | data[offset + 1]);
}

void parseEngineFrame1(const uint8_t *data, uint8_t len) {
    if (len < 7) return;
    SignalStore::update(SignalIds::RPM, static_cast<float>(readU16BE(data, 0)));
    SignalStore::update(SignalIds::THROTTLE_POS, data[2] * 0.5f);
    const float mapKpa = readU16BE(data, 3) * 0.1f;
    SignalStore::update(SignalIds::MAP_KPA, mapKpa);
    SignalStore::update(SignalIds::BOOST_BAR, (mapKpa - 100.0f) / 100.0f);
    SignalStore::update(SignalIds::IAT_C, static_cast<float>(data[5]) - 40.0f);
    if (len >= 8)
        SignalStore::update(SignalIds::SPEED_KPH, readU16BE(data, 6) * 0.1f);
}

void parseEngineFrame2(const uint8_t *data, uint8_t len) {
    if (len < 3) return;
    const float lambda = readU16BE(data, 0) * 0.001f;
    SignalStore::update(SignalIds::LAMBDA_1, lambda);
    SignalStore::update(SignalIds::AFR_1, lambda * 14.7f);
    SignalStore::update(SignalIds::GEAR, static_cast<float>(data[2]));
    if (len >= 5)
        SignalStore::update(SignalIds::FUEL_PRESS_BAR, readU16BE(data, 3) * 0.01f);
}

void parseTempsFrame(const uint8_t *data, uint8_t len) {
    if (len < 6) return;
    SignalStore::update(SignalIds::COOLANT_TEMP_C, readI16BE(data, 0) * 0.1f - 40.0f);
    SignalStore::update(SignalIds::OIL_TEMP_C, readI16BE(data, 2) * 0.1f - 40.0f);
    SignalStore::update(SignalIds::OIL_PRESS_BAR, readU16BE(data, 4) * 0.01f);
}

void parseElecFrame(const uint8_t *data, uint8_t len) {
    if (len < 2) return;
    SignalStore::update(SignalIds::BATTERY_VOLTS, readU16BE(data, 0) * 0.01f);
}

void parseFlagsFrame(const uint8_t *data, uint8_t len) {
    if (len < 1) return;
    const uint8_t flags = data[0];
    SignalStore::update(SignalIds::FLAG_MIL,          (flags >> 0) & 0x01);
    SignalStore::update(SignalIds::FLAG_LAUNCH_CTRL,  (flags >> 1) & 0x01);
    SignalStore::update(SignalIds::FLAG_FLAT_SHIFT,   (flags >> 2) & 0x01);
    SignalStore::update(SignalIds::FLAG_ANTI_LAG,     (flags >> 3) & 0x01);
    SignalStore::update(SignalIds::FLAG_TRACTION_CUT, (flags >> 4) & 0x01);
}

void parseMapInfoFrame(const uint8_t *data, uint8_t len) {
    if (len < 1) return;
    SignalStore::update(SignalIds::MAP_NUMBER, static_cast<float>(data[0]));
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void MaxxEcuParser::parseFrame(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (s_runtimeLoaded) {
        // Data-driven dispatch — processes all signals defined for this frame ID
        bool matched = false;
        for (uint8_t i = 0; i < s_runtimeCount; ++i) {
            if (s_runtime[i].canFrameId != frameId) continue;
            matched = true;
            const RuntimeSignal &sig = s_runtime[i];
            if (sig.startByte + sig.byteLength > length) continue;
            const float val = decodeBytes(data, sig.startByte, sig.byteLength,
                                          sig.bigEndian, sig.isSigned, sig.bitMask,
                                          sig.scale, sig.offset);
            SignalStore::update(sig.signalId, val);
        }
        if (matched) return;
    }

    // Fallback: hardcoded handlers for the default MaxxECU frame layout.
    // Active when signals.json has not been loaded or does not cover this frame ID.
    switch (frameId) {
        case FRAME_ID_ENGINE_1: parseEngineFrame1(data, length); break;
        case FRAME_ID_ENGINE_2: parseEngineFrame2(data, length); break;
        case FRAME_ID_TEMPS:    parseTempsFrame(data, length);   break;
        case FRAME_ID_ELEC:     parseElecFrame(data, length);    break;
        case FRAME_ID_FLAGS:    parseFlagsFrame(data, length);   break;
        case FRAME_ID_MAP_INFO: parseMapInfoFrame(data, length); break;
        default:
            // Unknown frame — MaxxECU broadcasts many frames we don't use
            break;
    }
}

void MaxxEcuParser::loadSignalDefinitions() {
    const CfgSignalConfig &cfg = ConfigLoader::getSignalConfig();
    if (!cfg.loaded || cfg.signalCount == 0) {
        LOG_WARN("CAN", "Signal config not loaded — using hardcoded defaults");
        return;
    }

    s_runtimeCount = 0;
    s_runtimeLoaded = false;

    for (uint8_t i = 0; i < cfg.signalCount && s_runtimeCount < CONFIG_MAX_SIGNALS; ++i) {
        const CfgSignalDef &def = cfg.signals[i];

        // Resolve signal name string → firmware SignalId
        SignalId sid = SignalIds::SIGNAL_COUNT; // sentinel = unknown
        for (uint8_t j = 0; j < kNameMapLen; ++j) {
            if (strcmp(def.name, kNameMap[j].name) == 0) {
                sid = kNameMap[j].id;
                break;
            }
        }
        if (sid == SignalIds::SIGNAL_COUNT) {
            LOG_WARN("CAN", "Unknown signal name '%s' in signals.json — skipping", def.name);
            continue;
        }

        RuntimeSignal &r = s_runtime[s_runtimeCount++];
        r.canFrameId = def.canFrameId;
        r.startByte  = def.startByte;
        r.byteLength = def.byteLength;
        r.bigEndian  = def.bigEndian;
        r.isSigned   = def.isSigned;
        r.scale      = def.scale;
        r.offset     = def.offset;
        r.bitMask    = def.bitMask;
        r.signalId   = sid;

        // Apply per-signal timeout from the config
        SignalStore::setTimeout(sid, def.timeoutMs);
    }

    s_runtimeLoaded = true;
    LOG_INFO("CAN", "Dynamic signal table loaded: %d signals from signals.json",
             s_runtimeCount);
}
