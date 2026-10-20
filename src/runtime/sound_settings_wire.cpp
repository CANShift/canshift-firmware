#include "sound_settings_wire.h"

#include "tone_sequencer_rs.h"

namespace {

constexpr char kVolumeKey[] = "volume";
constexpr char kSoundsKey[] = "sounds";
constexpr char kBootKey[] = "boot";
constexpr char kShiftToneKey[] = "shift_tone";

bool readVolume(JsonVariantConst v, uint8_t &out) {
    if (v.isNull())
        return true;
    if (!v.is<int>())
        return false;
    const int pct = v.as<int>();
    if (pct < 0 || pct > kToneVolumeMaxPct)
        return false;
    out = static_cast<uint8_t>(pct);
    return true;
}

bool readFlag(JsonVariantConst v, bool &out) {
    if (v.isNull())
        return true;
    if (!v.is<bool>())
        return false;
    out = v.as<bool>();
    return true;
}

bool readSounds(JsonVariantConst v, SoundSettingsWire::Values &out) {
    if (v.isNull())
        return true;
    if (!v.is<JsonObjectConst>())
        return false;
    const JsonObjectConst sounds = v.as<JsonObjectConst>();
    return readFlag(sounds[kBootKey], out.boot) && readFlag(sounds[kShiftToneKey], out.shiftTone);
}

}

SoundSettingsWire::Merge SoundSettingsWire::merge(JsonObjectConst settings, Values &values) {
    const JsonVariantConst volume = settings[kVolumeKey];
    const JsonVariantConst sounds = settings[kSoundsKey];
    if (volume.isNull() && sounds.isNull())
        return Merge::Absent;
    Values next = values;
    if (!readVolume(volume, next.volumePct) || !readSounds(sounds, next))
        return Merge::Invalid;
    values = next;
    return Merge::Changed;
}

void SoundSettingsWire::write(JsonObject out, const Values &values) {
    out[kVolumeKey] = values.volumePct;
    JsonObject sounds = out[kSoundsKey].to<JsonObject>();
    sounds[kBootKey] = values.boot;
    sounds[kShiftToneKey] = values.shiftTone;
}
