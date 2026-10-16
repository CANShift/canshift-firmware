#include "usb_comm_internal.h"

#include "board_config.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

namespace {

struct VerdictName {
    SignalsSource::Verdict verdict;
    const char *name;
};

constexpr VerdictName kVerdictNames[] = {
    {SignalsSource::Verdict::Unreadable, "not valid JSON"},
    {SignalsSource::Verdict::NoSignalList, "no signals array"},
    {SignalsSource::Verdict::TooMany, "more signals than the cap"},
    {SignalsSource::Verdict::SignalRejected, "a signal the loader would drop"},
};

const char *verdictName(SignalsSource::Verdict verdict) {
    for (const VerdictName &entry : kVerdictNames) {
        if (entry.verdict == verdict)
            return entry.name;
    }
    return "unknown";
}

bool acceptSignalsUpload(const char *stagedPath) {
    const SignalsSource::Verdict verdict = ConfigLoader::inspectSignalsFile(stagedPath);
    if (SignalsSource::clean(verdict))
        return true;
    LOG_WARN("USB", "PUT_FILE signals.json refused: %s", verdictName(verdict));
    return false;
}

} // namespace

void UsbCommInternal::commitSignalsUpload() {
    const StorageDriver::ChunkCommit result =
        StorageDriver::endChunkedWriteChecked(acceptSignalsUpload);
    if (result == StorageDriver::ChunkCommit::Rejected) {
        UsbComm::sendError("invalid_signals");
        return;
    }
    if (result == StorageDriver::ChunkCommit::Failed) {
        UsbComm::sendError("write_failed");
        return;
    }
    LOG_INFO("USB", "PUT_FILE signals.json accepted — rebooting");
    UsbCommInternal::rebootIntoSavedConfig();
}
