#include "app_config.h"
#if APP_AUDIO_ENABLED

    #include "sound_settings.h"

    #include "board.h"
    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "hal/storage/nvs_store.h"
    #include "tone_sequencer_rs.h"

    #include <Preferences.h>
    #include <atomic>

namespace {

constexpr char kNvsNs[] = "sound_cfg";
constexpr char kKeyVolume[] = "volume";
constexpr char kKeyBoot[] = "boot";
constexpr char kKeyShiftTone[] = "shift";

std::atomic<uint8_t> s_volumePct{SoundSettingsWire::kDefaults.volumePct};
std::atomic<bool> s_boot{SoundSettingsWire::kDefaults.boot};
std::atomic<bool> s_shiftTone{SoundSettingsWire::kDefaults.shiftTone};

void store(const SoundSettingsWire::Values &values) {
    s_volumePct.store(values.volumePct, std::memory_order_relaxed);
    s_boot.store(values.boot, std::memory_order_relaxed);
    s_shiftTone.store(values.shiftTone, std::memory_order_relaxed);
}

bool persist(const SoundSettingsWire::Values &values) {
    const bool saved = NvsStore::putUChar(kNvsNs, kKeyVolume, values.volumePct) &&
                       NvsStore::putBool(kNvsNs, kKeyBoot, values.boot) &&
                       NvsStore::putBool(kNvsNs, kKeyShiftTone, values.shiftTone);
    if (saved)
        return true;
    LOG_ERROR("AUDIO", "NVS write failed — sound settings not persisted");
    ErrorStore::push(ERROR_SRC_SYSTEM, "nvs_write", "sound settings not persisted");
    return false;
}

}

void SoundSettings::load() {
    Preferences p;
    SoundSettingsWire::Values values = SoundSettingsWire::kDefaults;
    if (p.begin(kNvsNs, true)) {
        values.volumePct = p.getUChar(kKeyVolume, values.volumePct);
        values.boot = p.getBool(kKeyBoot, values.boot);
        values.shiftTone = p.getBool(kKeyShiftTone, values.shiftTone);
        p.end();
    }
    if (values.volumePct > kToneVolumeMaxPct)
        values.volumePct = SoundSettingsWire::kDefaults.volumePct;
    store(values);
}

bool SoundSettings::available() {
    return canshift::boards::runtimeBoardProfile().audio.present;
}

SoundSettingsWire::Values SoundSettings::current() {
    return {s_volumePct.load(std::memory_order_relaxed), s_boot.load(std::memory_order_relaxed),
            s_shiftTone.load(std::memory_order_relaxed)};
}

void SoundSettings::writeTo(JsonObject out) {
    SoundSettingsWire::write(out, current());
}

bool SoundSettings::apply(JsonObjectConst settings) {
    if (!available())
        return true;
    SoundSettingsWire::Values values = current();
    const SoundSettingsWire::Merge merged = SoundSettingsWire::merge(settings, values);
    if (merged == SoundSettingsWire::Merge::Absent)
        return true;
    if (merged == SoundSettingsWire::Merge::Invalid) {
        LOG_WARN("AUDIO", "sound settings rejected — volume 0-100, boot/shift_tone booleans");
        return false;
    }
    if (!persist(values))
        return false;
    store(values);
    LOG_INFO("AUDIO", "sound settings applied — volume=%u%% boot=%d shift_tone=%d",
             static_cast<unsigned>(values.volumePct), values.boot, values.shiftTone);
    return true;
}

#endif
