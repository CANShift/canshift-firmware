#pragma once

#include <stdint.h>

namespace SignalsSource {

enum class Verdict : uint8_t {
    Ok = 0,
    Unreadable,
    NoSignalList,
    TooMany,
    SignalRejected,
};

enum class Source : uint8_t {
    None = 0,
    Primary,
    Backup,
};

[[nodiscard]] inline bool clean(Verdict verdict) {
    return verdict == Verdict::Ok;
}

[[nodiscard]] inline bool usable(Verdict verdict) {
    return verdict == Verdict::Ok || verdict == Verdict::TooMany ||
           verdict == Verdict::SignalRejected;
}

[[nodiscard]] inline Source choose(Verdict primary, Verdict backup) {
    if (clean(primary))
        return Source::Primary;
    if (clean(backup))
        return Source::Backup;
    if (usable(primary))
        return Source::Primary;
    if (usable(backup))
        return Source::Backup;
    return Source::None;
}

}
