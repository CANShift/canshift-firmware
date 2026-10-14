#include "reply_capture.h"

#include "diag/logger.h"
#include "hal/usb/usb_comm.h"
#include "hal/usb/usb_comm_internal.h"

#include <Arduino.h>

#include <cstring>

namespace {

constexpr char kLogPrefix[] = "{\"log\":";

ReplyCapture::Transport s_active = ReplyCapture::Transport::Ws;
std::vector<std::string> s_lines;
std::string s_serialPending;
size_t s_replyCount = 0;
bool s_recording = true;
std::vector<char> s_scratch;

bool isLogLine(const char *line, size_t len) {
    return len >= sizeof(kLogPrefix) - 1 && memcmp(line, kLogPrefix, sizeof(kLogPrefix) - 1) == 0;
}

void keepLine(ReplyCapture::Transport from, const char *line, size_t len) {
    if (from != s_active)
        return;
    while (len > 0 && line[len - 1] == '\n')
        --len;
    s_replyCount += isLogLine(line, len) ? 0 : 1;
    if (s_recording)
        s_lines.emplace_back(line, len);
}

void socketLine(const char *line, size_t len) {
    keepLine(ReplyCapture::Transport::Ws, line, len);
}

void serialByte(uint8_t c) {
    if (c != '\n') {
        s_serialPending.push_back(static_cast<char>(c));
        return;
    }
    keepLine(ReplyCapture::Transport::Usb, s_serialPending.data(), s_serialPending.size());
    s_serialPending.clear();
}

char *copyLine(const char *line) {
    const size_t len = strlen(line);
    s_scratch.resize(len + 1);
    memcpy(s_scratch.data(), line, len + 1);
    return s_scratch.data();
}

} // namespace

namespace ReplyCapture {

void install() {
    UsbComm::setSocketSink(&socketLine);
    Logger::setMirror(&socketLine);
    Serial.setCapture(&serialByte);
}

void clear() {
    s_lines.clear();
    s_serialPending.clear();
    s_replyCount = 0;
}

void setRecording(bool recording) {
    s_recording = recording;
}

const std::vector<std::string> &lines() {
    return s_lines;
}

size_t replyCount() {
    return s_replyCount;
}

void dispatch(Transport transport, const char *line) {
    s_active = transport;
    char *buf = copyLine(line);
    if (transport == Transport::Usb) {
        UsbCommInternal::dispatchFromSerial(buf);
        return;
    }
    UsbComm::dispatchFromSocket(buf);
}

} // namespace ReplyCapture
