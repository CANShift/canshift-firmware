#include "app_config.h"
#if APP_SD_DATALOG_ENABLED

    #include "datalog_file.h"

    #include "datalog_rs.h"
    #include "diag/logger.h"
    #include "hal/storage/sd_card.h"

    #include <errno.h>
    #include <fcntl.h>
    #include <stdio.h>
    #include <sys/stat.h>
    #include <unistd.h>

namespace DatalogFile {

namespace {

constexpr size_t kPathCapacity = 40;
constexpr uint16_t kSessionProbeLimit = 16;
constexpr mode_t kDirMode = 0755;
constexpr mode_t kFileMode = 0644;
constexpr int kNoFile = -1;

int s_fd = kNoFile;

Result fromErrno() {
    return errno == ENOSPC ? Result::Full : Result::Failed;
}

bool ensureLogDir() {
    if (mkdir(SdCard::kLogDir, kDirMode) == 0 || errno == EEXIST)
        return true;
    LOG_ERROR("DLOG", "mkdir %s failed: errno %d", SdCard::kLogDir, errno);
    return false;
}

int createExclusive(uint16_t session) {
    char name[kDatalogNameBytes + 1] = {0};
    if (datalog_log_name_rs(session, reinterpret_cast<uint8_t *>(name), kDatalogNameBytes) == 0)
        return kNoFile;
    char path[kPathCapacity];
    snprintf(path, sizeof(path), "%s/%s", SdCard::kLogDir, name);
    return ::open(path, O_WRONLY | O_CREAT | O_EXCL, kFileMode);
}

int createFresh(uint16_t &session) {
    for (uint16_t probe = 0; probe < kSessionProbeLimit; probe++, session++) {
        const int fd = createExclusive(session);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    return kNoFile;
}

Result writeHeader(uint16_t session, uint8_t *scratch, size_t capacity) {
    const size_t preamble = datalog_preamble_rs(scratch, capacity);
    const size_t header =
        datalog_header_rs(scratch + preamble, capacity - preamble, kDatalogSamplePeriodMs, session);
    if (preamble == 0 || header == 0) {
        LOG_ERROR("DLOG", "datalog header does not fit %u B", static_cast<unsigned>(capacity));
        return Result::Failed;
    }
    return append(scratch, preamble + header);
}

}

Result open(uint16_t &session, uint8_t *scratch, size_t capacity) {
    if (!ensureLogDir())
        return fromErrno();
    s_fd = createFresh(session);
    if (s_fd < 0) {
        LOG_ERROR("DLOG", "cannot create session %u: errno %d", static_cast<unsigned>(session),
                  errno);
        return fromErrno();
    }
    LOG_INFO("DLOG", "session %u opened", static_cast<unsigned>(session));
    return writeHeader(session, scratch, capacity);
}

Result append(const uint8_t *bytes, size_t len) {
    size_t written = 0;
    while (written < len) {
        const ssize_t n = ::write(s_fd, bytes + written, len - written);
        if (n <= 0)
            return n == 0 ? Result::Full : fromErrno();
        written += static_cast<size_t>(n);
    }
    return Result::Ok;
}

Result sync() {
    return ::fsync(s_fd) == 0 ? Result::Ok : fromErrno();
}

Result finish(uint32_t tMs, uint32_t dropped) {
    uint8_t record[kDatalogMaxMarkerBytes];
    const Result ended = append(record, datalog_end_rs(record, sizeof(record), tMs, dropped));
    const Result synced = ended == Result::Ok ? sync() : ended;
    const int closed = ::close(s_fd);
    s_fd = kNoFile;
    if (synced != Result::Ok)
        return synced;
    return closed == 0 ? Result::Ok : fromErrno();
}

void abandon() {
    if (s_fd < 0)
        return;
    if (::close(s_fd) != 0)
        LOG_WARN("DLOG", "close after a failed write: errno %d", errno);
    s_fd = kNoFile;
}

}

#endif
