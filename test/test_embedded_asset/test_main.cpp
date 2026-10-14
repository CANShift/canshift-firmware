#include "boot/embedded_asset.h"
#include "deflated_fixtures.h"
#include "hal/storage/storage_driver.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <unity.h>
#include <vector>

namespace {

constexpr const char *kPath = "/config/out.json";
constexpr size_t kLengthHeaderBytes = 4;
constexpr int kJsonLikeRecords = 600;
constexpr size_t kNoiseBytes = 6000;

using Outcome = EmbeddedAsset::Outcome;

std::vector<uint8_t> jsonLike() {
    std::string text;
    char record[32];
    for (int i = 0; i < kJsonLikeRecords; ++i) {
        snprintf(record, sizeof(record), "{\"id\":%d,\"v\":%d},", i, i * 7 % 13);
        text += record;
    }
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> noise() {
    std::vector<uint8_t> bytes;
    uint32_t state = 1;
    for (size_t i = 0; i < kNoiseBytes; ++i) {
        state = state * 1103515245u + 12345u;
        bytes.push_back(static_cast<uint8_t>(state >> 16));
    }
    return bytes;
}

Outcome inflate(const std::vector<uint8_t> &blob, size_t &length) {
    return EmbeddedAsset::inflateToFile(kPath, blob.data(), blob.data() + blob.size(), length);
}

template <size_t N>
std::vector<uint8_t> blobOf(const uint8_t (&bytes)[N]) {
    return std::vector<uint8_t>(bytes, bytes + N);
}

void assertFileEquals(const std::vector<uint8_t> &expected) {
    size_t length = 0;
    const uint8_t *data = StorageDriver::fakeRead(kPath, &length);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_UINT32(expected.size(), length);
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), data, length);
}

void assertNothingWritten() {
    size_t length = 0;
    TEST_ASSERT_NULL(StorageDriver::fakeRead(kPath, &length));
    TEST_ASSERT_FALSE(StorageDriver::isChunkedWriteOpen());
}

} // namespace

void setUp() {
    StorageDriver::fakeReset();
}

void tearDown() {}

void test_textLongerThanTheWindowInflatesByteForByte() {
    size_t length = 0;
    TEST_ASSERT_EQUAL(Outcome::Written, inflate(blobOf(fixtures::kJsonLikeDeflated), length));
    TEST_ASSERT_EQUAL_UINT32(jsonLike().size(), length);
    assertFileEquals(jsonLike());
}

void test_incompressibleBytesInflateByteForByte() {
    size_t length = 0;
    TEST_ASSERT_EQUAL(Outcome::Written, inflate(blobOf(fixtures::kNoiseDeflated), length));
    assertFileEquals(noise());
}

void test_flippedByteIsCorruptAndWritesNothing() {
    std::vector<uint8_t> blob = blobOf(fixtures::kJsonLikeDeflated);
    blob[blob.size() / 2] ^= 0x55;
    size_t length = 0;
    TEST_ASSERT_EQUAL(Outcome::Corrupt, inflate(blob, length));
    assertNothingWritten();
}

void test_truncatedStreamIsCorrupt() {
    std::vector<uint8_t> blob = blobOf(fixtures::kJsonLikeDeflated);
    blob.resize(blob.size() / 2);
    size_t length = 0;
    TEST_ASSERT_EQUAL(Outcome::Corrupt, inflate(blob, length));
    assertNothingWritten();
}

void test_lengthHeaderThatDisagreesIsCorrupt() {
    for (const int delta : {-1, 1}) {
        std::vector<uint8_t> blob = blobOf(fixtures::kJsonLikeDeflated);
        const uint32_t wrong = static_cast<uint32_t>(static_cast<int>(jsonLike().size()) + delta);
        memcpy(blob.data(), &wrong, sizeof(wrong));
        size_t length = 0;
        TEST_ASSERT_EQUAL(Outcome::Corrupt, inflate(blob, length));
        assertNothingWritten();
    }
}

void test_streamWiderThanTheWindowIsRefused() {
    size_t length = 0;
    TEST_ASSERT_EQUAL(Outcome::Corrupt, inflate(blobOf(fixtures::kJsonLikeFullWindow), length));
    assertNothingWritten();
}

void test_blobWithoutAStreamIsEmpty() {
    const std::vector<uint8_t> blob(kLengthHeaderBytes, 0);
    size_t length = 0;
    TEST_ASSERT_EQUAL(Outcome::Empty, inflate(blob, length));
    assertNothingWritten();
}

void test_failedStorageWriteAbortsTheFile() {
    StorageDriver::fakeFailChunksAfter(1000);
    size_t length = 0;
    TEST_ASSERT_EQUAL(Outcome::WriteFailed, inflate(blobOf(fixtures::kJsonLikeDeflated), length));
    assertNothingWritten();
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_textLongerThanTheWindowInflatesByteForByte);
    RUN_TEST(test_incompressibleBytesInflateByteForByte);
    RUN_TEST(test_flippedByteIsCorruptAndWritesNothing);
    RUN_TEST(test_truncatedStreamIsCorrupt);
    RUN_TEST(test_lengthHeaderThatDisagreesIsCorrupt);
    RUN_TEST(test_streamWiderThanTheWindowIsRefused);
    RUN_TEST(test_blobWithoutAStreamIsEmpty);
    RUN_TEST(test_failedStorageWriteAbortsTheFile);
    return UNITY_END();
}
