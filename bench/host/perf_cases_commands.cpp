#include "perf_cases.h"

#include "reply_capture.h"

namespace {

const char *s_line = nullptr;

}

namespace PerfCases {

bool setupCommand(const char *line) {
    s_line = line;
    ReplyCapture::setRecording(false);
    ReplyCapture::clear();
    ReplyCapture::dispatch(ReplyCapture::Transport::Ws, s_line);
    return ReplyCapture::replyCount() == 1;
}

void runCommand(uint32_t) {
    ReplyCapture::dispatch(ReplyCapture::Transport::Ws, s_line);
}

}
