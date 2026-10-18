
#include "logger.h"

#ifdef ARDUINO_USB_MODE
    #include <hal/usb_serial_jtag_ll.h>
    #include <soc/soc_caps.h>
#endif
#include "board_config.h"

#include <atomic>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef UNIT_TEST
    #include <freertos/task.h>
#endif

namespace {

SemaphoreHandle_t s_uartMutex = nullptr;

#if APP_WORKSHOP_ENABLED
std::atomic<Logger::LineMirror> s_mirror{nullptr};

void mirrorLine(const char *line, size_t len) {
    const Logger::LineMirror mirror = s_mirror.load(std::memory_order_acquire);
    if (mirror)
        mirror(line, len);
}
#else
void mirrorLine(const char *, size_t) {}
#endif

constexpr size_t kMsgBufSize = 256;
constexpr size_t kEscBufSize = 512;
constexpr size_t kLineBufSize = 640;
constexpr TickType_t kEmitLockTimeoutMs = 50;
constexpr size_t kNoRoom = SIZE_MAX;
constexpr char kLogLevels[] = "EWIDV";
constexpr char kDefaultLogLevel = 'I';

struct ShortEscape {
    unsigned char raw;
    char escaped;
};

constexpr ShortEscape kShortEscapes[] = {
    {'"', '"'}, {'\\', '\\'}, {'\n', 'n'}, {'\r', 'r'}, {'\t', 't'},
};

char shortEscapeFor(unsigned char c) {
    for (const ShortEscape &escape : kShortEscapes) {
        if (escape.raw == c)
            return escape.escaped;
    }
    return '\0';
}

size_t appendShortEscape(char escaped, char *dst, size_t w, size_t safeEnd) {
    if (w + 2 > safeEnd)
        return kNoRoom;
    dst[w] = '\\';
    dst[w + 1] = escaped;
    return w + 2;
}

size_t appendUnicodeEscape(unsigned char c, char *dst, size_t w, size_t safeEnd) {
    if (w + 6 > safeEnd)
        return kNoRoom;
    static const char hex[] = "0123456789abcdef";
    dst[w] = '\\';
    dst[w + 1] = 'u';
    dst[w + 2] = '0';
    dst[w + 3] = '0';
    dst[w + 4] = hex[(c >> 4) & 0x0F];
    dst[w + 5] = hex[c & 0x0F];
    return w + 6;
}

size_t appendJsonChar(unsigned char c, char *dst, size_t w, size_t safeEnd) {
    const char escaped = shortEscapeFor(c);
    if (escaped != '\0')
        return appendShortEscape(escaped, dst, w, safeEnd);
    if (c < 0x20)
        return appendUnicodeEscape(c, dst, w, safeEnd);
    if (w + 1 > safeEnd)
        return kNoRoom;
    dst[w] = static_cast<char>(c);
    return w + 1;
}

void escapeJson(const char *src, char *dst, size_t dstCap) {
    if (dstCap == 0)
        return;
    size_t w = 0;
    for (size_t r = 0; src[r] != '\0'; ++r) {
        const size_t next = appendJsonChar(static_cast<unsigned char>(src[r]), dst, w, dstCap - 1);
        if (next == kNoRoom)
            break;
        w = next;
    }
    dst[w] = '\0';
}

bool isTagChar(unsigned char c) {
    return c >= 0x20 && c < 0x7F && c != '"' && c != '\\';
}

void sanitizeTag(const char *tag, char *out) {
    size_t w = 0;
    for (size_t r = 0; tag && tag[r] != '\0' && w < kLogTagMaxLen; ++r) {
        const unsigned char c = static_cast<unsigned char>(tag[r]);
        if (isTagChar(c))
            out[w++] = static_cast<char>(c);
    }
    out[w] = '\0';
}

char normalizeLevel(char level) {
    const bool known = level != '\0' && strchr(kLogLevels, level) != nullptr;
    return known ? level : kDefaultLogLevel;
}

void formatMessage(char *msg, size_t cap, const char *fmt, va_list ap) {
    if (fmt)
        vsnprintf(msg, cap, fmt, ap);
    else
        msg[0] = '\0';
    msg[cap - 1] = '\0';
}

size_t formatLine(char level, const char *tag, const char *fmt, va_list ap, char *line) {
    static char msg[kMsgBufSize];
    formatMessage(msg, sizeof(msg), fmt, ap);
    static char escaped[kEscBufSize];
    escapeJson(msg, escaped, sizeof(escaped));
    static char tagBuf[kLogTagMaxLen + 1];
    sanitizeTag(tag, tagBuf);
    const int n = snprintf(
        line, kLineBufSize, "{\"log\":1,\"ts\":%lu,\"lvl\":\"%c\",\"tag\":\"%s\",\"msg\":\"%s\"}\n",
        static_cast<unsigned long>(millis()), normalizeLevel(level), tagBuf, escaped);
    if (n <= 0)
        return 0;
    return static_cast<size_t>(n) >= kLineBufSize ? kLineBufSize - 1 : static_cast<size_t>(n);
}

void assertUnlockedEmitOnUiCore() {
#ifndef UNIT_TEST
    if (!s_uartMutex)
        configASSERT(xPortGetCoreID() == kTaskCoreUi);
#endif
}

size_t writeLockedLine(char level, const char *tag, const char *fmt, va_list ap, char *line) {
    assertUnlockedEmitOnUiCore();
    const Logger::UartLock lock(pdMS_TO_TICKS(kEmitLockTimeoutMs));
    if (s_uartMutex && !lock.held())
        return 0;
    const size_t len = formatLine(level, tag, fmt, ap, line);
    if (len > 0) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        (void)Logger::writeAll(reinterpret_cast<const uint8_t *>(line), len);
    }
    return len;
}

// HWCDC::write returns 0 without re-arming the TX interrupt when its ring is
// full, so the ring stops draining. flush() re-arms it but waits forever when no
// host is reading, which would hang the boot — re-arm the interrupt directly.
void kickStalledTxDrain() {
#if defined(ARDUINO_USB_MODE) && SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
#else
    Serial.flush();
#endif
}

}

bool Logger::writeAll(const uint8_t *bytes, size_t len) {
    size_t sent = 0;
    const uint32_t deadline = millis() + kUsbTxWriteTimeoutMs;
    while (sent < len) {
        const size_t remaining = len - sent;
        const size_t piece = remaining < kUsbTxPieceBytes ? remaining : kUsbTxPieceBytes;
        const bool roomForPiece = static_cast<size_t>(Serial.availableForWrite()) >= piece;
        sent += roomForPiece ? Serial.write(bytes + sent, piece) : 0;
        if (sent >= len) {
            return true;
        }
        if (static_cast<int32_t>(millis() - deadline) >= 0) {
            return false;
        }
        kickStalledTxDrain();
        vTaskDelay(1);
    }
    return true;
}

void Logger::init() {
    if (!s_uartMutex) {
        s_uartMutex = xSemaphoreCreateRecursiveMutex();
    }
}

Logger::UartLock::UartLock(TickType_t timeout)
    : held_(s_uartMutex && xSemaphoreTakeRecursive(s_uartMutex, timeout) == pdTRUE) {}

Logger::UartLock::~UartLock() {
    if (held_)
        xSemaphoreGiveRecursive(s_uartMutex);
}

void Logger::emit(char level, const char *tag, const char *fmt, ...) {
    char line[kLineBufSize];
    va_list ap;
    va_start(ap, fmt);
    const size_t len = writeLockedLine(level, tag, fmt, ap, line);
    va_end(ap);
    if (len > 0)
        mirrorLine(line, len);
}

#if APP_WORKSHOP_ENABLED
void Logger::setMirror(LineMirror mirror) {
    s_mirror.store(mirror, std::memory_order_release);
}
#endif
