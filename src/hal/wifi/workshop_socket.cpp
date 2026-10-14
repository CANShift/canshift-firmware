#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_server_internal.h"

    #include "diag/logger.h"
    #include "hal/usb/usb_comm.h"
    #include "util/mem_alloc.h"

    #include <esp_heap_caps.h>
    #include <esp_http_server.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/queue.h>
    #include <unistd.h>

    #include <atomic>
    #include <memory>
    #include <stdint.h>

namespace WorkshopServerInternal {

namespace {

constexpr int kNoClient = -1;
constexpr UBaseType_t kLineQueueDepth = 2;
constexpr uint32_t kLineQueueWaitMs = 2000;
constexpr size_t kMaxLineBytes = USB_RX_BUF_SIZE;
constexpr char kLineEnd = '\n';

struct HeapFree {
    void operator()(char *line) const {
        heap_caps_free(line);
    }
};

using LineBuffer = std::unique_ptr<char, HeapFree>;

httpd_handle_t s_server = nullptr;
std::atomic<QueueHandle_t> s_lines{nullptr};
std::atomic<int> s_clientFd{kNoClient};

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
        LOG_WARN("WORKSHOP", "Socket send failed: %s", esp_err_to_name(err));
}

void closeSession(int fd) {
    if (httpd_sess_trigger_close(s_server, fd) != ESP_OK) {
        LOG_WARN("WORKSHOP", "Previous tuner socket %d could not be closed", fd);
        return;
    }
    LOG_INFO("WORKSHOP", "Previous tuner socket %d replaced", fd);
}

esp_err_t adoptClient(httpd_req_t *req) {
    const int fd = httpd_req_to_sockfd(req);
    const int previous = s_clientFd.exchange(fd);
    if (previous != kNoClient && previous != fd)
        closeSession(previous);
    LOG_INFO("WORKSHOP", "Tuner connected on socket %d", fd);
    return ESP_OK;
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
    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK || !frameAcceptable(frame)) {
        LOG_WARN("WORKSHOP", "Socket frame refused (type %d, %u B) — closing",
                 static_cast<int>(frame.type), static_cast<unsigned>(frame.len));
        return ESP_FAIL;
    }
    return frame.len == 0 ? ESP_OK : queueLine(req, frame);
}

} // namespace

esp_err_t handleSocket(httpd_req_t *req) {
    if (req->method == HTTP_GET)
        return adoptClient(req);
    return receiveLine(req);
}

void onSessionClosed(httpd_handle_t, int sockfd) {
    int expected = sockfd;
    if (s_clientFd.compare_exchange_strong(expected, kNoClient))
        LOG_INFO("WORKSHOP", "Tuner disconnected");
    if (close(sockfd) != 0)
        LOG_WARN("WORKSHOP", "Socket %d close failed", sockfd);
}

bool bindSocket(httpd_handle_t server) {
    s_server = server;
    const QueueHandle_t lines = xQueueCreate(kLineQueueDepth, sizeof(char *));
    if (!lines) {
        LOG_ERROR("WORKSHOP", "Socket line queue allocation failed");
        return false;
    }
    s_lines.store(lines);
    UsbComm::setSocketSink(sendToClient);
    return true;
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
}

} // namespace WorkshopServerInternal

#endif
