#pragma once

#include <stdint.h>

namespace OtaSessionWatch {

enum class Verdict : uint8_t {
    Keep = 0,
    IdleTimeout,
    LinkClosed,
};

inline constexpr uint32_t kIdleTimeoutMs = 15000;

struct Session {
    bool receiving;
    bool fromSocket;
    uint32_t lastActivityMs;
};

[[nodiscard]] inline Verdict judge(const Session &session, bool socketClosed, uint32_t nowMs) {
    if (!session.receiving)
        return Verdict::Keep;
    if (socketClosed && session.fromSocket)
        return Verdict::LinkClosed;
    if (nowMs - session.lastActivityMs > kIdleTimeoutMs)
        return Verdict::IdleTimeout;
    return Verdict::Keep;
}

} // namespace OtaSessionWatch
