// json_helpers.cpp — Low-level JSON / hex / color primitives shared by the
// ConfigLoader translation units (#1207).
//
// Extracted from `config_loader.cpp` so the orchestrator stays focused on
// PSRAM/rollback management and the per-type parsers stay focused on schema
// shape. Nothing here touches the shared `s_dashboard` / `s_signals` /
// `s_device` / `s_inputs` storage — every helper is pure with respect to
// inputs and outputs.

#include "config_loader_internal.h"

#include "app_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>

#ifdef ARDUINO
    #include <esp_heap_caps.h>
#endif

namespace ConfigLoaderInternal {

// Suffix for the boot-time fallback copy written by atomic saves.
namespace {
constexpr const char *kBakSuffix = ".bak";
// CFG_MAX_PATH_LEN + ".bak" + null terminator.
constexpr size_t kBakPathLen = CFG_MAX_PATH_LEN + 5;

bool buildBakPath(char *out, size_t outLen, const char *base) {
    if (!out || !base || outLen == 0)
        return false;
    const size_t baseLen = strlen(base);
    const size_t suffixLen = strlen(kBakSuffix);
    if (baseLen + suffixLen + 1 > outLen)
        return false;
    memcpy(out, base, baseLen);
    memcpy(out + baseLen, kBakSuffix, suffixLen);
    out[baseLen + suffixLen] = '\0';
    return true;
}
} // namespace

void logLargestFreeBlock(const char *where) {
#ifdef ARDUINO
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    LOG_INFO("CFG", "heap.largest_free=%u before %s", static_cast<unsigned>(largest), where);
#else
    (void)where;
#endif
}

// The implementation streams the SPIFFS File straight into the JsonDocument
// (no contiguous file-sized malloc). This eliminates the ~21 KB readFile()
// allocation that caused boot OOM with the LV_MEM_SIZE bump in #555
// (issue #576).
bool readAndParseWithBak(const char *path, JsonDocument &doc) {
    logLargestFreeBlock(path);

    if (StorageDriver::fileExists(path)) {
        DeserializationError err = StorageDriver::parseJsonFile(path, doc);
        if (!err)
            return true;
        // Both branches below collapse to a no-op when LOG_WARN is compiled
        // out (APP_LOG_LEVEL=1 in production + native env) but emit distinct
        // messages otherwise — false positive under the no-op preprocess path.
        // NOLINTNEXTLINE(bugprone-branch-clone)
        if (err == DeserializationError::EmptyInput) {
            LOG_WARN("CFG", "%s could not be opened — falling back to .bak", path);
        } else {
            LOG_WARN("CFG", "%s parse error: %s — falling back to .bak", path, err.c_str());
        }
    } else {
        LOG_WARN("CFG", "%s missing — falling back to .bak", path);
    }

    char bakPath[kBakPathLen];
    if (!buildBakPath(bakPath, sizeof(bakPath), path))
        return false;
    if (!StorageDriver::fileExists(bakPath))
        return false;

    doc.clear();
    DeserializationError bakErr = StorageDriver::parseJsonFile(bakPath, doc);
    if (bakErr) {
        LOG_ERROR("CFG", "%s also failed to parse: %s", bakPath, bakErr.c_str());
        return false;
    }

    LOG_WARN("CFG", "%s recovered from .bak", path);
    // Promote the .bak back into place so the next save creates a fresh .bak.
    if (!StorageDriver::renameFile(bakPath, path)) {
        LOG_WARN("CFG", "Could not rename %s back to %s — non-fatal", bakPath, path);
    }
    return true;
}

void parseColor(const char *hex, CfgColor *out) {
    if (!hex || hex[0] != '#') {
        out->rgb = 0x000000;
        return;
    }
    out->rgb = static_cast<uint32_t>(strtoul(hex + 1, nullptr, 16));
}

// Parse a single hex color ("#RRGGBB"). Returns the unsigned 0x00RRGGBB
// payload, or 0 on malformed input — callers tolerate fallback to black.
uint32_t parseHexColorValue(const char *hex) {
    if (!hex || hex[0] != '#')
        return 0u;
    return static_cast<uint32_t>(strtoul(hex + 1, nullptr, 16));
}

// Decode an even-length, lowercase-or-uppercase hex string into a byte buffer.
// Returns true and writes `*outLen` on success. Returns false (without
// touching `out`) on any of:
//   - hex == nullptr
//   - odd length
//   - decoded length > maxLen
//   - any non-hex character in `hex`
// Empty input is allowed and yields *outLen == 0.
bool decodeHexBytes(const char *hex, uint8_t *out, uint8_t maxLen, uint8_t *outLen) {
    if (!hex || !out || !outLen)
        return false;
    const size_t hexLen = strlen(hex);
    if (hexLen % 2 != 0)
        return false;
    const size_t byteLen = hexLen / 2;
    if (byteLen > maxLen)
        return false;

    for (size_t i = 0; i < byteLen; ++i) {
        char buf[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char *end = nullptr;
        const unsigned long v = strtoul(buf, &end, 16);
        if (end != buf + 2 || v > 0xFFu)
            return false;
        out[i] = static_cast<uint8_t>(v);
    }
    *outLen = static_cast<uint8_t>(byteLen);
    return true;
}

} // namespace ConfigLoaderInternal
