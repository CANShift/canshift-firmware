#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_server_internal.h"

    #include "diag/logger.h"
    #include "hal/usb/usb_comm.h"
    #include "util/mem_alloc.h"
    #include "workshop_mode_rs.h"

    #include <Arduino.h>
    #include <esp_http_server.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/queue.h>
    #include <unistd.h>

    #include <atomic>
    #include <memory>
    #include <stdint.h>
    #include <string.h>

namespace WorkshopServerInternal {

namespace {

constexpr int kNoClient = -1;
constexpr UBaseType_t kLineQueueDepth = 2;
constexpr UBaseType_t kLogQueueDepth = 16;
constexpr uint32_t kLineQueueWaitMs = 2000;
constexpr size_t kMaxLineBytes = kUsbRxBufSize;
constexpr char kLineEnd = '\n';
constexpr uint16_t kClosePolicyViolation = 1008;
constexpr uint16_t kBusyCloseCode = 4001;
constexpr char kBusyCloseReason[] = "busy";
constexpr size_t kCloseCodeBytes = 2;
constexpr size_t kBusyCloseReasonLen = sizeof(kBusyCloseReason) - 1;
constexpr unsigned kByteBits = 8;
constexpr uint16_t kByteMask = 0xFF;

using LineBuffer = Mem::HeapPtr<char>;

httpd_handle_t s_server = nullptr;
std::atomic<QueueHandle_t> s_lines{nullptr};
std::atomic<QueueHandle_t> s_logLines{nullptr};
std::atomic<int> s_clientFd{kNoClient};
std::atomic<uint32_t> s_lastActivityMs{0};
std::atomic<uint32_t> s_droppedLogs{0};

void dropClient(int fd, esp_err_t err) {
    int expected = fd;
    if (!s_clientFd.compare_exchange_strong(expected, kNoClient))
        return;
    if (httpd_sess_trigger_close(s_server, fd) != ESP_OK)
        LOG_WARN("WORKSHOP", "Socket %d could not be closed", fd);
    LOG_WARN("WORKSHOP", "Socket send failed (%s) — tuner dropped", esp_err_to_name(err));
}

void sendToClient(const char *line, size_t len) {
    const int fd = s_clientFd.load();
    if (fd == kNoClient)
        return;
    if (len > 0 && line[len - 1] == kLineEnd)
        --len;
    httpd_ws_frame_t frame = {};
    frame.final = true;
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(line));
    frame.len = len;
    const esp_err_t err = httpd_ws_send_frame_async(s_server, fd, &frame);
    if (err != ESP_OK)
        dropClient(fd, err);
}

void mirrorLog(const char *line, size_t len) {
    const QueueHandle_t logs = s_logLines.load();
    if (!logs || s_clientFd.load() == kNoClient)
        return;
    LineBuffer copy(static_cast<char *>(Mem::allocPreferSpiram(len + 1)));
    if (!copy) {
        s_droppedLogs.fetch_add(1);
        return;
    }
    memcpy(copy.get(), line, len);
    copy.get()[len] = '\0';
    char *raw = copy.get();
    if (xQueueSend(logs, &raw, 0) != pdTRUE) {
        s_droppedLogs.fetch_add(1);
        return;
    }
    copy.release();
}

void reportDroppedLogs() {
    const uint32_t dropped = s_droppedLogs.exchange(0);
    if (dropped > 0)
        LOG_WARN("WORKSHOP", "%lu log line(s) not mirrored to the tuner",
                 static_cast<unsigned long>(dropped));
}

void drainLogs() {
    char *raw = nullptr;
    while (xQueueReceive(s_logLines.load(), &raw, 0) == pdTRUE) {
        const LineBuffer line(raw);
        sendToClient(line.get(), strlen(line.get()));
    }
    reportDroppedLogs();
}

void closeSession(int fd) {
    if (httpd_sess_trigger_close(s_server, fd) != ESP_OK) {
        LOG_WARN("WORKSHOP", "Previous tuner socket %d could not be closed", fd);
        return;
    }
    LOG_INFO("WORKSHOP", "Stale tuner socket %d replaced", fd);
}

esp_err_t refuseClient(httpd_req_t *req) {
    LOG_WARN("WORKSHOP", "Socket %d refused — foreign Origin", httpd_req_to_sockfd(req));
    uint8_t reason[] = {static_cast<uint8_t>(kClosePolicyViolation >> 8),
                        static_cast<uint8_t>(kClosePolicyViolation & 0xFF)};
    httpd_ws_frame_t frame = {};
    frame.final = true;
    frame.type = HTTPD_WS_TYPE_CLOSE;
    frame.payload = reason;
    frame.len = sizeof(reason);
    if (httpd_ws_send_frame(req, &frame) != ESP_OK)
        LOG_WARN("WORKSHOP", "Close frame not sent to the refused socket");
    return ESP_FAIL;
}

void sendBusyClose(httpd_req_t *req) {
    uint8_t payload[kCloseCodeBytes + kBusyCloseReasonLen];
    payload[0] = static_cast<uint8_t>(kBusyCloseCode >> kByteBits);
    payload[1] = static_cast<uint8_t>(kBusyCloseCode & kByteMask);
    memcpy(payload + kCloseCodeBytes, kBusyCloseReason, kBusyCloseReasonLen);
    httpd_ws_frame_t frame = {};
    frame.final = true;
    frame.type = HTTPD_WS_TYPE_CLOSE;
    frame.payload = payload;
    frame.len = sizeof(payload);
    if (httpd_ws_send_frame(req, &frame) != ESP_OK)
        LOG_WARN("WORKSHOP", "Busy close frame not sent");
}

esp_err_t adopt(httpd_req_t *, int fd, int) {
    s_lastActivityMs.store(millis());
    s_clientFd.store(fd);
    LOG_INFO("WORKSHOP", "Tuner connected on socket %d", fd);
    return ESP_OK;
}

esp_err_t replaceStale(httpd_req_t *req, int fd, int previous) {
    closeSession(previous);
    UsbComm::noteSocketClosed();
    return adopt(req, fd, previous);
}

esp_err_t refuseBusy(httpd_req_t *req, int fd, int current) {
    LOG_WARN("WORKSHOP", "Second tuner on socket %d refused — socket %d is active", fd, current);
    sendBusyClose(req);
    if (httpd_sess_trigger_close(s_server, fd) != ESP_OK)
        LOG_WARN("WORKSHOP", "Refused socket %d could not be closed", fd);
    return ESP_OK;
}

using AdmissionHandler = esp_err_t (*)(httpd_req_t *req, int fd, int current);

constexpr AdmissionHandler kAdmissionHandlers[] = {adopt, replaceStale, refuseBusy};

static_assert(sizeof(kAdmissionHandlers) / sizeof(kAdmissionHandlers[0]) ==
                  kWorkshopAdmitRefuseBusy + 1,
              "every socket admission needs a handler");

esp_err_t adoptClient(httpd_req_t *req) {
    if (!originAllowed(req))
        return refuseClient(req);
    const int fd = httpd_req_to_sockfd(req);
    const int current = s_clientFd.load();
    const uint8_t admission = workshop_socket_admission_rs(current != kNoClient, current == fd,
                                                           UsbComm::socketTransferInFlight(),
                                                           s_lastActivityMs.load(), millis());
    if (admission > kWorkshopAdmitRefuseBusy)
        return ESP_FAIL;
    return kAdmissionHandlers[admission](req, fd, current);
}

bool frameAcceptable(const httpd_ws_frame_t &frame) {
    return frame.type == HTTPD_WS_TYPE_TEXT && frame.final && frame.len < kMaxLineBytes;
}

bool enqueue(LineBuffer &line, size_t len) {
    line.get()[len] = '\0';
    char *raw = line.get();
    if (xQueueSend(s_lines.load(), &raw, pdMS_TO_TICKS(kLineQueueWaitMs)) != pdTRUE)
        return false;
    line.release();
    return true;
}

esp_err_t queueLine(httpd_req_t *req, httpd_ws_frame_t &frame) {
    LineBuffer line(static_cast<char *>(Mem::allocPreferSpiram(frame.len + 1)));
    frame.payload = reinterpret_cast<uint8_t *>(line.get());
    if (!line || httpd_ws_recv_frame(req, &frame, frame.len) != ESP_OK ||
        !enqueue(line, frame.len)) {
        LOG_WARN("WORKSHOP", "Socket line of %u B dropped — closing",
                 static_cast<unsigned>(frame.len));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t receiveLine(httpd_req_t *req) {
    if (httpd_req_to_sockfd(req) != s_clientFd.load()) {
        LOG_WARN("WORKSHOP", "Frame from a socket that is not the tuner — closing");
        return ESP_FAIL;
    }
    s_lastActivityMs.store(millis());
    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK || !frameAcceptable(frame)) {
        LOG_WARN("WORKSHOP", "Socket frame refused (type %d, %u B) — closing",
                 static_cast<int>(frame.type), static_cast<unsigned>(frame.len));
        return ESP_FAIL;
    }
    return frame.len == 0 ? ESP_OK : queueLine(req, frame);
}

}

esp_err_t handleSocket(httpd_req_t *req) {
    if (req->method == HTTP_GET)
        return adoptClient(req);
    return receiveLine(req);
}

void onSessionClosed(httpd_handle_t, int sockfd) {
    int expected = sockfd;
    if (s_clientFd.compare_exchange_strong(expected, kNoClient)) {
        LOG_INFO("WORKSHOP", "Tuner disconnected");
        UsbComm::noteSocketClosed();
    }
    if (close(sockfd) != 0)
        LOG_WARN("WORKSHOP", "Socket %d close failed", sockfd);
}

bool bindSocket(httpd_handle_t server) {
    s_server = server;
    const QueueHandle_t lines = xQueueCreate(kLineQueueDepth, sizeof(char *));
    const QueueHandle_t logs = xQueueCreate(kLogQueueDepth, sizeof(char *));
    if (!lines || !logs) {
        LOG_ERROR("WORKSHOP", "Socket queue allocation failed");
        return false;
    }
    s_logLines.store(logs);
    s_lines.store(lines);
    UsbComm::setSocketSink(sendToClient);
    Logger::setMirror(mirrorLog);
    return true;
}

int socketOwnerFd() {
    return s_clientFd.load();
}

bool socketBound() {
    return s_lines.load() != nullptr;
}

void drainSocket() {
    char *raw = nullptr;
    while (xQueueReceive(s_lines.load(), &raw, 0) == pdTRUE) {
        const LineBuffer line(raw);
        UsbComm::dispatchFromSocket(line.get());
    }
    drainLogs();
}

}

#endif
