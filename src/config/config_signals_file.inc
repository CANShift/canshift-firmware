#include "config_loader_internal.h"

#include "board_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"
#include "json_reader.h"
#include "signals_source.h"
#include "util/json_writer.h"

#include <ArduinoJson.h>
#include <memory>
#include <new>

namespace ConfigLoaderInternal {

namespace {

using SignalsSource::Source;
using SignalsSource::Verdict;

Verdict readSignalsFile(const char *path, CfgSignalConfig *into) {
    if (path[0] == '\0' || !StorageDriver::fileExists(path))
        return Verdict::Unreadable;
    logLargestFreeBlock(path);
    JsonDocument doc(JsonReader::configAllocator());
    if (StorageDriver::parseJsonFile(path, doc))
        return Verdict::Unreadable;
    return parseSignalsDocument(doc, into);
}

bool loadNoSignals(const char *) {
    LOG_ERROR("CFG", "signals.json unusable (primary + .bak)");
    ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", "signals.json unreadable");
    return false;
}

bool loadPrimarySignals(const char *) {
    return SignalsSource::usable(readSignalsFile(kConfigPathSignals, &s_signals));
}

bool promoteSignalsBackup(const char *bakPath) {
    return StorageDriver::removeFile(kConfigPathSignals) &&
           StorageDriver::renameFile(bakPath, kConfigPathSignals);
}

bool loadBackupSignals(const char *bakPath) {
    LOG_ERROR("CFG", "signals.json does not load — falling back to %s", bakPath);
    ErrorStore::push(ERROR_SRC_CONFIG, "SIG_BAK", "signals.json restored from .bak");
    if (promoteSignalsBackup(bakPath))
        return loadPrimarySignals(nullptr);
    LOG_WARN("CFG", "%s could not replace signals.json — loading it in place", bakPath);
    return SignalsSource::usable(readSignalsFile(bakPath, &s_signals));
}

using SignalsLoader = bool (*)(const char *bakPath);

constexpr SignalsLoader kSignalsLoaders[] = {loadNoSignals, loadPrimarySignals, loadBackupSignals};

static_assert(sizeof(kSignalsLoaders) / sizeof(kSignalsLoaders[0]) ==
                  static_cast<size_t>(Source::Backup) + 1,
              "every signals source needs a loader");

} // namespace

bool loadSignals() {
    char bakPath[kBakPathLen] = {};
    if (!buildBakPath(bakPath, sizeof(bakPath), kConfigPathSignals))
        LOG_WARN("CFG", "signals.json backup path does not fit — no fallback");
    const Verdict primary = readSignalsFile(kConfigPathSignals, nullptr);
    const Verdict backup =
        SignalsSource::clean(primary) ? Verdict::Unreadable : readSignalsFile(bakPath, nullptr);
    const Source source = SignalsSource::choose(primary, backup);
    return kSignalsLoaders[static_cast<size_t>(source)](bakPath);
}

} // namespace ConfigLoaderInternal

SignalsSource::Verdict ConfigLoader::inspectSignalsFile(const char *path) {
    if (path == nullptr)
        return SignalsSource::Verdict::Unreadable;
    return ConfigLoaderInternal::readSignalsFile(path, nullptr);
}

namespace {

struct VerdictName {
    SignalsSource::Verdict verdict;
    const char *name;
};

constexpr VerdictName kVerdictNames[] = {
    {SignalsSource::Verdict::Unreadable, "not valid JSON"},
    {SignalsSource::Verdict::NoSignalList, "no signals array"},
    {SignalsSource::Verdict::TooMany, "more signals than the cap"},
    {SignalsSource::Verdict::SignalRejected, "a signal the loader would drop"},
};

const char *verdictName(SignalsSource::Verdict verdict) {
    for (const VerdictName &entry : kVerdictNames) {
        if (entry.verdict == verdict)
            return entry.name;
    }
    return "unknown";
}

bool readCurrentOut(JsonDocument &into) {
    if (!StorageDriver::fileExists(kConfigPathSignals))
        return false;
    JsonDocument current(JsonReader::configAllocator());
    if (StorageDriver::parseJsonFile(kConfigPathSignals, current))
        return false;
    JsonVariantConst out = current["out"];
    return !out.isNull() && into.set(out);
}

bool writeDocument(const char *path, const JsonDocument &doc) {
    const size_t length = JsonWriter::measure(doc.as<JsonVariantConst>());
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[length + 1]);
    if (!buffer)
        return false;
    const size_t written =
        JsonWriter::toBuffer(doc.as<JsonVariantConst>(), buffer.get(), length + 1);
    return written == length && StorageDriver::writeFileAtomic(
                                    path, reinterpret_cast<const uint8_t *>(buffer.get()), length);
}

bool mergeCurrentOut(const char *stagedPath, const JsonDocument &out) {
    JsonDocument staged(JsonReader::configAllocator());
    if (StorageDriver::parseJsonFile(stagedPath, staged))
        return false;
    if (!staged["out"].isNull())
        return true;
    return staged["out"].set(out.as<JsonVariantConst>()) && writeDocument(stagedPath, staged);
}

bool keepCurrentOut(const char *stagedPath) {
    JsonDocument out(JsonReader::configAllocator());
    if (!readCurrentOut(out))
        return true;
    if (mergeCurrentOut(stagedPath, out))
        return true;
    LOG_ERROR("CFG", "signals.json upload: could not carry the current out block into %s",
              stagedPath);
    ErrorStore::push(ERROR_SRC_CONFIG, "SIG_OUT", "signals.json out block not kept");
    return false;
}

} // namespace

bool ConfigLoader::acceptSignalsUpload(const char *stagedPath) {
    const SignalsSource::Verdict verdict = inspectSignalsFile(stagedPath);
    if (SignalsSource::clean(verdict))
        return keepCurrentOut(stagedPath);
    LOG_WARN("CFG", "signals.json upload refused: %s", verdictName(verdict));
    return false;
}
