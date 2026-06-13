
#include "logger.h"
#include "board_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef UNIT_TEST
    #include <freertos/task.h>
#endif

namespace {

SemaphoreHandle_t s_uartMutex = nullptr;

constexpr size_t MSG_BUF_SIZE = 256;
constexpr size_t ESC_BUF_SIZE = 512;
constexpr size_t LINE_BUF_SIZE = 640;

void escapeJson(const char *src, char *dst, size_t dstCap) {
    if (dstCap == 0)
        return;
    size_t w = 0;
    const size_t safeEnd = dstCap - 1;
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

    if (!s_uartMutex) {
#ifndef UNIT_TEST
        configASSERT(xPortGetCoreID() == TASK_CORE_UI);
#endif
    } else if (xSemaphoreTakeRecursive(s_uartMutex, pdMS_TO_TICKS(50)) != pdTRUE) {

        return;
    }

    static char msg[MSG_BUF_SIZE];
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
    } else {
        msg[0] = '\0';
    }
    msg[sizeof(msg) - 1] = '\0';

    static char escaped[ESC_BUF_SIZE];
    escapeJson(msg, escaped, sizeof(escaped));

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

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        Serial.write(reinterpret_cast<const uint8_t *>(line), toWrite);
    }

    if (s_uartMutex) {
        xSemaphoreGiveRecursive(s_uartMutex);
    }
}
