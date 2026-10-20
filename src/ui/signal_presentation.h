#pragma once

#include <stddef.h>

namespace SignalPresentation {

struct Entry {
    const char *signalId;
    const char *kicker;
    const char *unit;
};

const Entry *entries();

size_t entryCount();

const char *kickerForSignal(const char *signalId);

const char *unitForSignal(const char *signalId);

void fallbackKicker(const char *signalId, char *out, size_t outLen);

}
