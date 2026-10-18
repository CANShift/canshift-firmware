#include "transcript_mode.h"

#include "bench_args.h"
#include "bench_runtime.h"
#include "reply_capture.h"

#include "esp_system.h"
#include "bench_clock.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr uint32_t kEpochMs = 1000;
constexpr uint32_t kCommandGapMs = 10;
constexpr size_t kLineBytes = 64 * 1024;

FILE *s_out = nullptr;
unsigned s_restarts = 0;

void noteRestart() {
    ++s_restarts;
}

bool parseTransport(const char *name, ReplyCapture::Transport &out) {
    if (strcmp(name, "usb") == 0) {
        out = ReplyCapture::Transport::Usb;
        return true;
    }
    out = ReplyCapture::Transport::Ws;
    return strcmp(name, "ws") == 0;
}

std::string trimmed(const char *line) {
    std::string text(line);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    return text;
}

void runCommand(ReplyCapture::Transport transport, const std::string &command) {
    const unsigned restartsBefore = s_restarts;
    ReplyCapture::clear();
    ReplyCapture::dispatch(transport, command.c_str());
    fprintf(s_out, "> %s\n", command.c_str());
    for (const std::string &reply : ReplyCapture::lines())
        fprintf(s_out, "< %s\n", reply.c_str());
    if (s_restarts != restartsBefore)
        fprintf(s_out, "~ restart\n");
    BenchClock::advanceMs(kCommandGapMs);
}

bool replay(FILE *in, ReplyCapture::Transport transport) {
    static char line[kLineBytes];
    while (fgets(line, sizeof(line), in) != nullptr) {
        const std::string command = trimmed(line);
        if (command.size() + 1 >= sizeof(line)) {
            fprintf(stderr, "bench: transcript line longer than %zu bytes\n", sizeof(line));
            return false;
        }
        if (!command.empty())
            runCommand(transport, command);
    }
    return true;
}

bool bootTranscript(const char *dataRoot) {
    BenchClock::useManual(kEpochMs);
    BenchRestart::hook() = &noteRestart;
    return BenchRuntime::boot(dataRoot);
}

}

namespace TranscriptMode {

int run(int argc, char **argv) {
    const char *inPath = BenchArgs::value(argc, argv, "--in", nullptr);
    const char *outPath = BenchArgs::value(argc, argv, "--out", nullptr);
    ReplyCapture::Transport transport = ReplyCapture::Transport::Ws;
    if (inPath == nullptr || outPath == nullptr ||
        !parseTransport(BenchArgs::value(argc, argv, "--transport", ""), transport))
        return 2;
    FILE *in = fopen(inPath, "r");
    s_out = fopen(outPath, "w");
    const bool opened = in != nullptr && s_out != nullptr;
    const bool ok = opened && bootTranscript(BenchArgs::value(argc, argv, "--data", "data")) &&
                    replay(in, transport);
    if (in != nullptr)
        fclose(in);
    if (s_out != nullptr)
        fclose(s_out);
    return ok ? 0 : 1;
}

}
