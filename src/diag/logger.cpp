// logger.cpp — JSON-line log emitter with UART0 mutex.
//
// Every LOG_* call funnels through Logger::emit(), which serializes the line
// as `{"log":1,"lvl":"X","tag":"...","msg":"..."}\n` and writes it under a
// FreeRTOS mutex shared with the wire-protocol producer (UsbComm). This keeps
// log lines from interleaving with telemetry / can / ack frames on UART0.
//
// The mutex is recursive so re-entrant logger calls from the same task — e.g.
// an assert / panic handler invoking LOG_ERROR while the task already holds
// the lock via UsbComm::lockUart() — cannot deadlock and silently drop the
// line that triggered the assert (F-HI-6, umbrella #1014).
//
// Pre-init boot text from the Arduino core / ESP-IDF can still land on UART0
// before Logger::init() runs — the studio drops malformed JSON silently, so
// this is non-fatal and intentionally not addressed here.

#include "logger.h"
#include "board_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

// One recursive mutex shared with all UART0 writers. Created in Logger::init().
// Recursive so a task already holding the lock (e.g. UsbComm streaming a
// multi-call frame) can still log from a nested call site without deadlocking.
SemaphoreHandle_t s_uartMutex = nullptr;

// Stack-allocated buffers in emit() are sized for the worst case:
//  - 256 B for the formatted message text (truncated beyond)
//  - 512 B for the JSON-escaped message (every byte → \u00XX = 6× expansion)
//  - 640 B for the final envelope (escape + small framing overhead)
constexpr size_t MSG_BUF_SIZE = 256;
constexpr size_t ESC_BUF_SIZE = 512;
constexpr size_t LINE_BUF_SIZE = 640;

// JSON-escape `src` (NUL-terminated) into `dst` of capacity `dstCap`.
// Always NUL-terminates `dst` and never writes past it. Truncates the input
// rather than failing if the destination is too small.
void escapeJson(const char *src, char *dst, size_t dstCap) {
    if (dstCap == 0)
        return;
    size_t w = 0;
    const size_t safeEnd = dstCap - 1; // reserve for '\0'
    for (size_t r = 0; src[r] != '\0'; ++r) {
        const unsigned char c = static_cast<unsigned char>(src[r]);
        const char *escape = nullptr;
        char twoChar[3] = {'\\', 0, 0};
        switch (c) {
            case '"':
                twoChar[1] = '"';
                escape = twoChar;
                break;
            case '\\':
                twoChar[1] = '\\';
                escape = twoChar;
                break;
            case '\n':
                twoChar[1] = 'n';
                escape = twoChar;
                break;
            case '\r':
                twoChar[1] = 'r';
                escape = twoChar;
                break;
            case '\t':
                twoChar[1] = 't';
                escape = twoChar;
                break;
            default:
                break;
        }
        if (escape) {
            if (w + 2 > safeEnd)
                break;
            dst[w++] = escape[0];
            dst[w++] = escape[1];
        } else if (c < 0x20) {
            if (w + 6 > safeEnd)
                break;
            // \u00XX
            static const char hex[] = "0123456789abcdef";
            dst[w++] = '\\';
            dst[w++] = 'u';
            dst[w++] = '0';
            dst[w++] = '0';
            dst[w++] = hex[(c >> 4) & 0x0F];
            dst[w++] = hex[c & 0x0F];
        } else {
            if (w + 1 > safeEnd)
                break;
            dst[w++] = static_cast<char>(c);
        }
    }
    dst[w] = '\0';
}

} // namespace

void Logger::init() {
    // Serial is already initialized in main.cpp setup() before this runs.
    if (!s_uartMutex) {
        s_uartMutex = xSemaphoreCreateRecursiveMutex();
    }
}

bool Logger::lockUart(TickType_t timeout) {
    if (!s_uartMutex)
        return false;
    return xSemaphoreTakeRecursive(s_uartMutex, timeout) == pdTRUE;
}

void Logger::unlockUart() {
    if (!s_uartMutex)
        return;
    xSemaphoreGiveRecursive(s_uartMutex);
}

void Logger::emit(char level, const char *tag, const char *fmt, ...) {
    // If init() hasn't run yet (very early boot), fall through and write
    // unprotected — the only writer at that point is the boot path itself.
    const bool locked =
        s_uartMutex ? (xSemaphoreTakeRecursive(s_uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) : false;
    if (s_uartMutex && !locked) {
        // Drop the line on contention rather than risk reordering or stalls.
        return;
    }

    // Static buffers protected by s_uartMutex (recursive, so a re-entrant
    // emit() from the same task — e.g. inside an assert handler — re-enters
    // safely instead of timing out and dropping the line). Keeping them
    // static avoids ~1.4 KB of stack on every call, which would be brutal
    // on the 2 KB sim / input task stacks.
    static char msg[MSG_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);
    msg[sizeof(msg) - 1] = '\0';

    static char escaped[ESC_BUF_SIZE];
    escapeJson(msg, escaped, sizeof(escaped));

    // Sanitize tag — keep printable ASCII only and cap at LOG_TAG_MAX_LEN.
    static char tagBuf[LOG_TAG_MAX_LEN + 1];
    {
        size_t w = 0;
        if (tag) {
            for (size_t r = 0; tag[r] != '\0' && w < LOG_TAG_MAX_LEN; ++r) {
                const unsigned char c = static_cast<unsigned char>(tag[r]);
                if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
                    tagBuf[w++] = static_cast<char>(c);
                }
            }
        }
        tagBuf[w] = '\0';
    }

    // Whitelist level letters — anything else collapses to 'I'.
    char lvl;
    switch (level) {
        case 'E':
        case 'W':
        case 'I':
        case 'D':
        case 'V':
            lvl = level;
            break;
        default:
            lvl = 'I';
            break;
    }

    char line[LINE_BUF_SIZE];
    const int n =
        snprintf(line, sizeof(line), "{\"log\":1,\"lvl\":\"%c\",\"tag\":\"%s\",\"msg\":\"%s\"}\n",
                 lvl, tagBuf, escaped);
    if (n > 0) {
        const size_t toWrite =
            (static_cast<size_t>(n) >= sizeof(line)) ? sizeof(line) - 1 : static_cast<size_t>(n);
        Serial.write(reinterpret_cast<const uint8_t *>(line), toWrite);
    }

    if (locked) {
        xSemaphoreGiveRecursive(s_uartMutex);
    }
}
