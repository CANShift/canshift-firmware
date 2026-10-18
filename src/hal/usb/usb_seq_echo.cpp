#include "usb_seq_echo.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr char kSeqKey[] = ",\"seq\":";
constexpr size_t kSeqKeyLen = sizeof(kSeqKey) - 1;
constexpr char kStatusPrefix[] = "{\"status\":";
constexpr size_t kStatusPrefixLen = sizeof(kStatusPrefix) - 1;

std::atomic<TaskHandle_t> s_task{nullptr};
UsbSeqEcho::Seq s_seq = {};

bool isStatusObject(const char *line, size_t len) {
    if (len <= kStatusPrefixLen || line[len - 1] != '}')
        return false;
    return strncmp(line, kStatusPrefix, kStatusPrefixLen) == 0;
}

}

namespace UsbSeqEcho {

Seq parseAfterCmd(const char *afterCmd) {
    if (afterCmd == nullptr || strncmp(afterCmd, kSeqKey, kSeqKeyLen) != 0)
        return {};
    const char *digits = afterCmd + kSeqKeyLen;
    if (*digits < '0' || *digits > '9')
        return {};
    const unsigned long long parsed = strtoull(digits, nullptr, 10);
    if (parsed > UINT32_MAX)
        return {};
    return {true, static_cast<uint32_t>(parsed)};
}

size_t tag(const char *line, size_t len, Seq seq, char *out, size_t cap) {
    if (!seq.present || line == nullptr || !isStatusObject(line, len))
        return 0;
    const int n = snprintf(out, cap, "%.*s,\"seq\":%lu}", static_cast<int>(len - 1), line,
                           static_cast<unsigned long>(seq.value));
    if (n <= 0 || static_cast<size_t>(n) >= cap)
        return 0;
    return static_cast<size_t>(n);
}

Scope::Scope(Seq seq) : armed_(seq.present) {
    if (!armed_)
        return;
    s_seq = seq;
    s_task.store(xTaskGetCurrentTaskHandle(), std::memory_order_release);
}

Scope::~Scope() {
    if (!armed_)
        return;
    s_task.store(nullptr, std::memory_order_release);
    s_seq = {};
}

Seq active() {
    if (s_task.load(std::memory_order_acquire) != xTaskGetCurrentTaskHandle())
        return {};
    return s_seq;
}

}
