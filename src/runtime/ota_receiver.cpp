#include "ota_receiver.h"

#include "diag/logger.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace OtaReceiver {

namespace {

constexpr uint8_t ESP_IMAGE_MAGIC = 0xe9;
constexpr uint32_t APP_DESC_MAGIC = 0xabcd5432;
constexpr size_t APP_DESC_OFFSET = 0x20;
constexpr size_t APP_HEADER_PROBE_BYTES = APP_DESC_OFFSET + sizeof(uint32_t);

State s_state = State::Idle;
const esp_partition_t *s_targetPartition = nullptr;
esp_ota_handle_t s_otaHandle = 0;
size_t s_expectedSize = 0;
size_t s_writtenSize = 0;
mbedtls_sha256_context s_shaCtx;
uint8_t s_expectedSha[32] = {0};
uint8_t s_headerProbe[APP_HEADER_PROBE_BYTES] = {0};
size_t s_headerProbeLen = 0;

void captureHeaderProbe(const uint8_t *data, size_t len) {
    if (s_headerProbeLen >= APP_HEADER_PROBE_BYTES)
        return;
    const size_t room = APP_HEADER_PROBE_BYTES - s_headerProbeLen;
    const size_t take = len < room ? len : room;
    memcpy(s_headerProbe + s_headerProbeLen, data, take);
    s_headerProbeLen += take;
}

bool headerProbeRejects() {
    if (s_headerProbeLen < APP_HEADER_PROBE_BYTES)
        return false;
    if (s_headerProbe[0] != ESP_IMAGE_MAGIC)
        return true;
    uint32_t descMagic = 0;
    memcpy(&descMagic, s_headerProbe + APP_DESC_OFFSET, sizeof(descMagic));
    return descMagic != APP_DESC_MAGIC;
}

bool startShaContext() {
    mbedtls_sha256_free(&s_shaCtx);
    mbedtls_sha256_init(&s_shaCtx);
    return mbedtls_sha256_starts_ret(&s_shaCtx, 0) == 0;
}

void teardownOtaHandle() {
    if (s_otaHandle != 0) {
        esp_ota_abort(s_otaHandle);
        s_otaHandle = 0;
    }
    s_targetPartition = nullptr;
    mbedtls_sha256_free(&s_shaCtx);
    s_expectedSize = 0;
    s_writtenSize = 0;
    s_headerProbeLen = 0;
}

WriteResult refuseChunk(const char *reason) {
    return {false, reason, s_writtenSize};
}

WriteResult abortWrite(const char *reason) {
    teardownOtaHandle();
    s_state = State::Failed;
    return {false, reason, s_writtenSize};
}

} // namespace

State state() {
    return s_state;
}

size_t expectedSize() {
    return s_expectedSize;
}

size_t writtenSize() {
    return s_writtenSize;
}

BeginResult begin(size_t totalSize, const uint8_t expectedSha256[32]) {
    if (s_state == State::Receiving) {
        LOG_WARN("OTA", "begin while already receiving — aborting previous session");
        teardownOtaHandle();
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) {
        s_state = State::Failed;
        return {false, "no_ota_partition"};
    }
    if (totalSize < APP_HEADER_PROBE_BYTES || totalSize > target->size) {
        s_state = State::Failed;
        return {false, "size_out_of_range"};
    }

    esp_ota_handle_t handle = 0;
    const esp_err_t err = esp_ota_begin(target, totalSize, &handle);
    if (err != ESP_OK) {
        LOG_ERROR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_state = State::Failed;
        return {false, "ota_begin_failed"};
    }

    s_targetPartition = target;
    s_otaHandle = handle;
    s_expectedSize = totalSize;
    s_writtenSize = 0;
    s_headerProbeLen = 0;
    memcpy(s_expectedSha, expectedSha256, 32);
    if (!startShaContext()) {
        LOG_ERROR("OTA", "sha256 engine start failed");
        teardownOtaHandle();
        s_state = State::Failed;
        return {false, "sha_engine_failed"};
    }
    s_state = State::Receiving;

    LOG_INFO("OTA", "begin: target='%s' size=%u", target->label, static_cast<unsigned>(totalSize));
    return {true, nullptr};
}

WriteResult writeChunk(uint32_t offset, const uint8_t *data, size_t len) {
    if (s_state != State::Receiving)
        return refuseChunk("not_receiving");
    if (data == nullptr || len == 0)
        return refuseChunk("empty_chunk");
    if (offset != s_writtenSize)
        return refuseChunk("offset_mismatch");
    if (s_writtenSize + len > s_expectedSize)
        return refuseChunk("overrun");

    captureHeaderProbe(data, len);
    if (headerProbeRejects()) {
        LOG_ERROR("OTA", "not an app image — rejecting at offset=%u",
                  static_cast<unsigned>(offset));
        return abortWrite("not_app_image");
    }

    const esp_err_t err = esp_ota_write(s_otaHandle, data, len);
    if (err != ESP_OK) {
        LOG_ERROR("OTA", "esp_ota_write failed at offset=%u: %s", static_cast<unsigned>(offset),
                  esp_err_to_name(err));
        return abortWrite("ota_write_failed");
    }

    if (mbedtls_sha256_update_ret(&s_shaCtx, data, len) != 0) {
        LOG_ERROR("OTA", "sha256 update failed at offset=%u", static_cast<unsigned>(offset));
        return abortWrite("sha_engine_failed");
    }
    s_writtenSize += len;
    return {true, nullptr, s_writtenSize};
}

CommitResult commit() {
    if (s_state != State::Receiving)
        return {false, "not_receiving", 0};
    if (s_writtenSize != s_expectedSize) {
        LOG_ERROR("OTA", "commit while incomplete: written=%u expected=%u",
                  static_cast<unsigned>(s_writtenSize), static_cast<unsigned>(s_expectedSize));
        teardownOtaHandle();
        s_state = State::Failed;
        return {false, "incomplete", 0};
    }

    uint8_t actualSha[32] = {0};
    if (mbedtls_sha256_finish_ret(&s_shaCtx, actualSha) != 0) {
        LOG_ERROR("OTA", "sha256 finish failed");
        teardownOtaHandle();
        s_state = State::Failed;
        return {false, "sha_engine_failed", 0};
    }
    if (memcmp(actualSha, s_expectedSha, 32) != 0) {
        LOG_ERROR("OTA", "sha256 mismatch — aborting");
        teardownOtaHandle();
        s_state = State::Failed;
        return {false, "sha256_mismatch", 0};
    }

    const esp_err_t endErr = esp_ota_end(s_otaHandle);
    s_otaHandle = 0;
    if (endErr != ESP_OK) {
        LOG_ERROR("OTA", "esp_ota_end failed: %s (0x%x)", esp_err_to_name(endErr),
                  static_cast<unsigned>(endErr));
        s_targetPartition = nullptr;
        s_state = State::Failed;
        return {false, "ota_end_failed", static_cast<int>(endErr)};
    }

    const esp_err_t setErr = esp_ota_set_boot_partition(s_targetPartition);
    if (setErr != ESP_OK) {
        LOG_ERROR("OTA", "esp_ota_set_boot_partition failed: %s (0x%x)", esp_err_to_name(setErr),
                  static_cast<unsigned>(setErr));
        s_targetPartition = nullptr;
        s_state = State::Failed;
        return {false, "set_boot_failed", static_cast<int>(setErr)};
    }

    LOG_INFO("OTA", "commit: new boot partition='%s' — restart will load it",
             s_targetPartition->label);
    s_targetPartition = nullptr;
    s_state = State::Committed;
    return {true, nullptr, 0};
}

void abort(const char *reason) {
    if (s_state != State::Receiving)
        return;
    LOG_WARN("OTA", "abort: %s", reason != nullptr ? reason : "<none>");
    teardownOtaHandle();
    s_state = State::Failed;
}

} // namespace OtaReceiver
