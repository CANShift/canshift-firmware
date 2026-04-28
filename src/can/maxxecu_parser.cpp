// maxxecu_parser.cpp — MaxxECU CAN protocol parser implementation

#include "maxxecu_parser.h"
#include "signal_map.h"
#include "runtime/signal_store.h"
#include "diag/logger.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

    inline uint16_t readU16BE(const uint8_t* data, uint8_t offset) {
        return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
    }

    inline int16_t readI16BE(const uint8_t* data, uint8_t offset) {
        return static_cast<int16_t>((data[offset] << 8) | data[offset + 1]);
    }

    void parseEngineFrame1(const uint8_t* data, uint8_t len) {
        if (len < 7) return;

        // RPM: bytes 0-1, uint16, 1 RPM/bit
        uint16_t rpm = readU16BE(data, 0);
        SignalStore::update(SignalIds::RPM, static_cast<float>(rpm));

        // TPS: byte 2, 0.5 %/bit
        float tps = data[2] * 0.5f;
        SignalStore::update(SignalIds::THROTTLE_POS, tps);

        // MAP: bytes 3-4, uint16, 0.1 kPa/bit
        float mapKpa = readU16BE(data, 3) * 0.1f;
        SignalStore::update(SignalIds::MAP_KPA, mapKpa);

        // Boost: derived from MAP
        float boostBar = (mapKpa - 100.0f) / 100.0f;  // relative to atmosphere
        SignalStore::update(SignalIds::BOOST_BAR, boostBar);

        // IAT: byte 5, 1 °C/bit, offset -40
        float iat = static_cast<float>(data[5]) - 40.0f;
        SignalStore::update(SignalIds::IAT_C, iat);

        // Speed: bytes 6-7, uint16, 0.1 km/h per bit
        if (len >= 8) {
            float speed = readU16BE(data, 6) * 0.1f;
            SignalStore::update(SignalIds::SPEED_KPH, speed);
        }
    }

    void parseEngineFrame2(const uint8_t* data, uint8_t len) {
        if (len < 3) return;

        // Lambda: bytes 0-1, uint16, lambda * 1000 (e.g. 1000 = 1.000)
        float lambda = readU16BE(data, 0) * 0.001f;
        SignalStore::update(SignalIds::LAMBDA_1, lambda);
        SignalStore::update(SignalIds::AFR_1, lambda * 14.7f);

        // Gear: byte 2
        SignalStore::update(SignalIds::GEAR, static_cast<float>(data[2]));

        // Fuel pressure: bytes 3-4, 0.01 bar/bit
        if (len >= 5) {
            float fuelPress = readU16BE(data, 3) * 0.01f;
            SignalStore::update(SignalIds::FUEL_PRESS_BAR, fuelPress);
        }
    }

    void parseTempsFrame(const uint8_t* data, uint8_t len) {
        if (len < 6) return;

        // Coolant: bytes 0-1, int16, 0.1 °C/bit, offset -40
        float coolant = readI16BE(data, 0) * 0.1f - 40.0f;
        SignalStore::update(SignalIds::COOLANT_TEMP_C, coolant);

        // Oil temp: bytes 2-3
        float oilTemp = readI16BE(data, 2) * 0.1f - 40.0f;
        SignalStore::update(SignalIds::OIL_TEMP_C, oilTemp);

        // Oil pressure: bytes 4-5, 0.01 bar/bit
        float oilPress = readU16BE(data, 4) * 0.01f;
        SignalStore::update(SignalIds::OIL_PRESS_BAR, oilPress);
    }

    void parseElecFrame(const uint8_t* data, uint8_t len) {
        if (len < 2) return;

        // Battery: bytes 0-1, 0.01 V/bit
        float volts = readU16BE(data, 0) * 0.01f;
        SignalStore::update(SignalIds::BATTERY_VOLTS, volts);
    }

    void parseFlagsFrame(const uint8_t* data, uint8_t len) {
        if (len < 1) return;

        uint8_t flags = data[0];
        SignalStore::update(SignalIds::FLAG_MIL,          (flags >> 0) & 0x01);
        SignalStore::update(SignalIds::FLAG_LAUNCH_CTRL,  (flags >> 1) & 0x01);
        SignalStore::update(SignalIds::FLAG_FLAT_SHIFT,   (flags >> 2) & 0x01);
        SignalStore::update(SignalIds::FLAG_ANTI_LAG,     (flags >> 3) & 0x01);
        SignalStore::update(SignalIds::FLAG_TRACTION_CUT, (flags >> 4) & 0x01);
    }

    void parseMapInfoFrame(const uint8_t* data, uint8_t len) {
        if (len < 1) return;
        // Map number: byte 0
        SignalStore::update(SignalIds::MAP_NUMBER, static_cast<float>(data[0]));
    }

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void MaxxEcuParser::parseFrame(uint32_t frameId, const uint8_t* data, uint8_t length) {
    // TODO: Replace hardcoded frame IDs with runtime-loaded signal definitions
    //       (see loadSignalDefinitions() below)
    switch (frameId) {
        case FRAME_ID_ENGINE_1: parseEngineFrame1(data, length); break;
        case FRAME_ID_ENGINE_2: parseEngineFrame2(data, length); break;
        case FRAME_ID_TEMPS:    parseTempsFrame(data, length);   break;
        case FRAME_ID_ELEC:     parseElecFrame(data, length);    break;
        case FRAME_ID_FLAGS:    parseFlagsFrame(data, length);   break;
        case FRAME_ID_MAP_INFO: parseMapInfoFrame(data, length); break;
        default:
            // Unknown frame — not an error, MaxxECU broadcasts many frames
            break;
    }
}

void MaxxEcuParser::loadSignalDefinitions() {
    // TODO: Load dynamic signal definitions from ConfigLoader::getSignalConfig()
    //       and build a runtime dispatch table rather than the hardcoded switch above.
    //       This will allow the desktop app to reconfigure CAN mappings without
    //       recompiling the firmware.
    LOG_INFO("CAN", "Signal definitions loaded (static defaults — TODO: load from signals.json)");
}
