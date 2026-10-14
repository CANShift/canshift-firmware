#include "usb_comm_internal.h"

#include "app_config.h"
#include "diag/logger.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace {

#ifdef ARDUINO

void serialSink(const char *data, size_t len) {
    if (!data || len == 0)
        return;
    const bool needsNewline = data[len - 1] != '\n';
    if (!Logger::lockUart(pdMS_TO_TICKS(USB_TX_LOCK_TIMEOUT_MS))) {
        return;
    }
    static constexpr uint8_t kNewline = '\n';
    if (Logger::writeAll(reinterpret_cast<const uint8_t *>(data), len) && needsNewline) {
        (void)Logger::writeAll(&kNewline, 1);
    }
    Logger::unlockUart();
}
#else
void serialSink(const char *, size_t) {}
#endif

#if APP_WORKSHOP_ENABLED

enum class ReplyRoute : uint8_t { Broadcast, Serial, Socket };

struct RouteSinks {
    bool serial;
    bool socket;
};

constexpr uint8_t kRouteCount = 3;
constexpr RouteSinks kRouteSinks[kRouteCount] = {{true, true}, {true, false}, {false, true}};
static_assert(static_cast<uint8_t>(ReplyRoute::Socket) + 1 == kRouteCount,
              "every reply route needs one sink row");

std::atomic<UsbComm::LineSink> s_socketSink{nullptr};
std::atomic<TaskHandle_t> s_routeTask{nullptr};
ReplyRoute s_route = ReplyRoute::Broadcast;

class RouteScope {
  public:
    explicit RouteScope(ReplyRoute route) {
        s_route = route;
        s_routeTask.store(xTaskGetCurrentTaskHandle());
    }
    ~RouteScope() {
        s_routeTask.store(nullptr);
        s_route = ReplyRoute::Broadcast;
    }
    RouteScope(const RouteScope &) = delete;
    RouteScope &operator=(const RouteScope &) = delete;
};

ReplyRoute currentRoute() {
    return s_routeTask.load() == xTaskGetCurrentTaskHandle() ? s_route : ReplyRoute::Broadcast;
}

void socketSink(const char *line, size_t len) {
    const UsbComm::LineSink sink = s_socketSink.load(std::memory_order_acquire);
    if (sink)
        sink(line, len);
}

#endif

} // namespace

#if APP_WORKSHOP_ENABLED

void UsbCommInternal::routeLine(const char *line, size_t len) {
    const RouteSinks &sinks = kRouteSinks[static_cast<uint8_t>(currentRoute())];
    if (sinks.serial)
        serialSink(line, len);
    if (sinks.socket)
        socketSink(line, len);
}

void UsbCommInternal::dispatchFromSerial(const char *jsonLine) {
    const RouteScope scope(ReplyRoute::Serial);
    handleCommand(jsonLine);
}

void UsbComm::dispatchFromSocket(const char *jsonLine) {
    const RouteScope scope(ReplyRoute::Socket);
    UsbCommInternal::handleCommand(jsonLine);
}

void UsbComm::setSocketSink(LineSink sink) {
    s_socketSink.store(sink, std::memory_order_release);
}

#else

void UsbCommInternal::routeLine(const char *line, size_t len) {
    serialSink(line, len);
}

void UsbCommInternal::dispatchFromSerial(const char *jsonLine) {
    handleCommand(jsonLine);
}

#endif
