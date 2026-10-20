#include "usb_comm_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"
#include "util/mem_alloc.h"

#include <freertos/FreeRTOS.h>

#include <memory>
#include <stdint.h>
#include <string.h>

namespace {

constexpr char kPrefix[] = "{\"status\":\"ok\",\"config\":";
constexpr char kSuffix[] = "}";
constexpr char kNewline[] = "\n";
constexpr size_t kPrefixBytes = sizeof(kPrefix) - 1;
constexpr size_t kSuffixBytes = sizeof(kSuffix) - 1;

using LineBuffer = Mem::HeapPtr<char>;

class SerialPrint : public Print {
  public:
    size_t write(uint8_t b) override {
        return Logger::writeAll(&b, 1) ? 1 : 0;
    }
    size_t write(const uint8_t *data, size_t len) override {
        return Logger::writeAll(data, len) ? len : 0;
    }
};

class BufferPrint : public Print {
  public:
    BufferPrint(char *dst, size_t cap) : dst_(dst), cap_(cap), used_(0) {}
    size_t write(uint8_t b) override {
        return write(&b, 1);
    }
    size_t write(const uint8_t *data, size_t len) override {
        const size_t room = cap_ - used_;
        const size_t n = len < room ? len : room;
        memcpy(dst_ + used_, data, n);
        used_ += n;
        return n;
    }
    size_t used() const {
        return used_;
    }

  private:
    char *dst_;
    size_t cap_;
    size_t used_;
};

bool writeText(Print &out, const char *text, size_t len) {
    return out.write(reinterpret_cast<const uint8_t *>(text), len) == len;
}

size_t writeReply(Print &out) {
    if (!writeText(out, kPrefix, kPrefixBytes))
        return 0;
    const size_t streamed = StorageDriver::streamFileTo(kConfigPathDashboard, out, true);
    if (streamed == 0 || !writeText(out, kSuffix, kSuffixBytes))
        return 0;
    return streamed;
}

struct SerialStream {
    bool locked;
    bool ended;
    size_t streamed;
};

SerialStream streamUnderUartLock() {
    const Logger::UartLock lock(pdMS_TO_TICKS(kUsbTxLockTimeoutMs));
    if (!lock.held())
        return {false, false, 0};
    SerialPrint sink;
    const size_t streamed = writeReply(sink);
    return {true, streamed > 0 && writeText(sink, kNewline, 1), streamed};
}

void streamConfigToSerial() {
    const SerialStream result = streamUnderUartLock();
    if (!result.locked) {
        LOG_WARN("USB", "GET_CONFIG: serial busy");
        UsbComm::sendError("busy");
        return;
    }
    const size_t streamed = result.streamed;
    if (!result.ended) {
        LOG_ERROR("USB", "GET_CONFIG: stream to host failed after %u B",
                  static_cast<unsigned>(streamed));
        return;
    }
    LOG_INFO("USB", "GET_CONFIG: sent %u bytes", static_cast<unsigned>(streamed));
}

void sendConfigToSocket(size_t fileBytes) {
    const size_t cap = kPrefixBytes + fileBytes + kSuffixBytes;
    const LineBuffer line(static_cast<char *>(Mem::allocPreferSpiram(cap)));
    if (!line) {
        LOG_ERROR("USB", "GET_CONFIG: no %u B buffer for the socket reply",
                  static_cast<unsigned>(cap));
        UsbComm::sendError("no_memory");
        return;
    }
    BufferPrint sink(line.get(), cap);
    if (writeReply(sink) != fileBytes || sink.used() != cap) {
        LOG_ERROR("USB", "GET_CONFIG: config read failed for the socket reply");
        UsbComm::sendError("read_failed");
        return;
    }
    UsbCommInternal::sendToSocket(line.get(), sink.used());
    LOG_INFO("USB", "GET_CONFIG: sent %u bytes to the socket", static_cast<unsigned>(fileBytes));
}

}

namespace UsbCommInternal {

void handleGetConfig() {
    const size_t fileBytes = StorageDriver::fileExists(kConfigPathDashboard)
                                 ? StorageDriver::fileSize(kConfigPathDashboard)
                                 : 0;
    if (fileBytes == 0) {
        LOG_WARN("USB", "GET_CONFIG: missing / empty %s", kConfigPathDashboard);
        UsbComm::sendError("config_not_found");
        return;
    }
    if (replyReachesSocket())
        sendConfigToSocket(fileBytes);
    if (replyReachesSerial())
        streamConfigToSerial();
}

}
