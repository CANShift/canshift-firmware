#include "perf_cases.h"

#include <iterator>

namespace {

constexpr const char *kOtaRejectLine =
    R"({"cmd":48,"total":4096,"sha256":"0000000000000000000000000000000000000000000000000000000000000000","sig":"AAAA"})";

constexpr PerfCases::Case kCases[] = {
    {"calibration", "ns/op", nullptr, &PerfCases::setupNothing, &PerfCases::runCalibration},
    {"can_decode", "ns/frame", nullptr, &PerfCases::setupCanDecode, &PerfCases::runCanDecode},
    {"expr_eval", "ns/eval", nullptr, &PerfCases::setupExprEval, &PerfCases::runExprEval},
    {"telemetry_encode", "ns/frame", nullptr, &PerfCases::setupNothing,
     &PerfCases::runTelemetryEncode},
    {"status_writer", "ns/line", nullptr, &PerfCases::setupNothing, &PerfCases::runStatusWriter},
    {"format_float", "ns/call", nullptr, &PerfCases::setupNothing, &PerfCases::runFormatFloat},
    {"command/status", "ns/command", R"({"cmd":16})", &PerfCases::setupCommand,
     &PerfCases::runCommand},
    {"command/ping", "ns/command", R"({"cmd":17})", &PerfCases::setupCommand,
     &PerfCases::runCommand},
    {"command/shared", "ns/command", R"({"cmd":9,"day":true})", &PerfCases::setupCommand,
     &PerfCases::runCommand},
    {"command/config_get", "ns/command", R"({"cmd":3})", &PerfCases::setupCommand,
     &PerfCases::runCommand},
    {"command/ota_reject", "ns/command", kOtaRejectLine, &PerfCases::setupCommand,
     &PerfCases::runCommand},
    {"command/unknown", "ns/command", R"({"cmd":200})", &PerfCases::setupCommand,
     &PerfCases::runCommand},
    {"command/parse_error", "ns/command", R"({"cmd":16,)", &PerfCases::setupCommand,
     &PerfCases::runCommand},
};

}

namespace PerfCases {

CaseList all() {
    return {kCases, std::size(kCases)};
}

}
