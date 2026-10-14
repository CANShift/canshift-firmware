#include "reply_capture.h"

#include "hal/usb/usb_comm.h"

#include <cstring>

namespace {

std::vector<std::string> s_lines;
size_t s_lineCount = 0;
bool s_recording = true;
std::vector<char> s_scratch;

void captureLine(const char *line, size_t len) {
    ++s_lineCount;
    if (!s_recording)
        return;
    while (len > 0 && line[len - 1] == '\n')
        --len;
    s_lines.emplace_back(line, len);
}

} // namespace

namespace ReplyCapture {

void install() {
    UsbComm::setSocketSink(&captureLine);
}

void clear() {
    s_lines.clear();
    s_lineCount = 0;
}

void setRecording(bool recording) {
    s_recording = recording;
}

const std::vector<std::string> &lines() {
    return s_lines;
}

size_t lineCount() {
    return s_lineCount;
}

void dispatch(const char *line) {
    const size_t len = strlen(line);
    s_scratch.resize(len + 1);
    memcpy(s_scratch.data(), line, len + 1);
    UsbComm::dispatchFromSocket(s_scratch.data());
}

} // namespace ReplyCapture
