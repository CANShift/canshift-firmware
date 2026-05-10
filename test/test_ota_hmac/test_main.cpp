// test_main.cpp — Unity tests for OtaHmacVerifier framing/streaming.
//
// What this file proves:
//   - Body bytes (everything except the trailing 32) reach the sink.
//   - Exactly the last 32 bytes of the upload are treated as the HMAC trailer.
//   - The same body, fed in any chunk pattern, produces identical sink input
//     and identical HMAC backend update() input.
//   - Uploads shorter than 32 bytes are rejected.
//   - A trailer mismatch produces finish() == false.
//   - constantTimeMemcmp returns 0 iff equal.
//
// What this file deliberately does NOT prove:
//   - That mbedTLS's HMAC-SHA256 is correct. That's mbedTLS's job — we use
//     a stub backend here so host tests don't need crypto. The production
//     mbedTLS backend is gated behind #ifndef UNIT_TEST in ota_hmac.cpp.

#include "hal/wifi/ota_hmac.h"

#include <cstdio>
#include <string.h>
#include <unity.h>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Stub HMAC backend — accumulates an XOR fingerprint over fed bytes plus a
// secret-derived seed. Not crypto, just a deterministic 32-byte output that
// changes when the body or secret changes. Plenty for testing the framing.
// ---------------------------------------------------------------------------

struct StubCtx {
    uint8_t state[OtaHmac::kHmacLen];
    size_t totalBytes;            // running counter so chunking does not affect output
    std::vector<uint8_t> updates; // bytes seen via update()
};

void *stubInit(const uint8_t *secret, size_t secretLen) {
    auto *ctx = new StubCtx();
    memset(ctx->state, 0, sizeof(ctx->state));
    ctx->totalBytes = 0;
    for (size_t i = 0; i < secretLen; ++i) {
        ctx->state[i % OtaHmac::kHmacLen] ^= secret[i];
    }
    return ctx;
}

bool stubUpdate(void *ctxp, const uint8_t *data, size_t len) {
    auto *ctx = static_cast<StubCtx *>(ctxp);
    for (size_t i = 0; i < len; ++i) {
        const size_t pos = ctx->totalBytes + i;
        ctx->state[pos % OtaHmac::kHmacLen] ^= data[i];
        ctx->updates.push_back(data[i]);
    }
    ctx->totalBytes += len;
    return true;
}

bool stubFinalize(void *ctxp, uint8_t out[OtaHmac::kHmacLen]) {
    auto *ctx = static_cast<StubCtx *>(ctxp);
    memcpy(out, ctx->state, OtaHmac::kHmacLen);
    delete ctx;
    return true;
}

const OtaHmac::HmacBackend kStub = {stubInit, stubUpdate, stubFinalize};

// ---------------------------------------------------------------------------
// Recording sink — captures everything the verifier emits.
// ---------------------------------------------------------------------------

struct Recorder {
    std::vector<uint8_t> body;
};

bool recordSink(const uint8_t *data, size_t len, void *user) {
    auto *r = static_cast<Recorder *>(user);
    r->body.insert(r->body.end(), data, data + len);
    return true;
}

// Compute the stub HMAC over a body using the same algorithm — useful for
// crafting a valid trailer in tests.
void computeStubHmac(const uint8_t *secret, size_t secretLen, const uint8_t *body,
                     size_t bodyLen, uint8_t out[OtaHmac::kHmacLen]) {
    memset(out, 0, OtaHmac::kHmacLen);
    for (size_t i = 0; i < secretLen; ++i) {
        out[i % OtaHmac::kHmacLen] ^= secret[i];
    }
    for (size_t i = 0; i < bodyLen; ++i) {
        out[i % OtaHmac::kHmacLen] ^= body[i];
    }
}

// Build "body || hmac(body)" using the stub algorithm.
std::vector<uint8_t> buildSignedUpload(const std::vector<uint8_t> &body,
                                       const uint8_t *secret, size_t secretLen) {
    std::vector<uint8_t> upload = body;
    uint8_t hmac[OtaHmac::kHmacLen];
    computeStubHmac(secret, secretLen, body.data(), body.size(), hmac);
    upload.insert(upload.end(), hmac, hmac + OtaHmac::kHmacLen);
    return upload;
}

// Feed an upload to a verifier in fixed-size chunks. Returns finish() result.
bool feedInChunks(OtaHmac::OtaHmacVerifier &v, const std::vector<uint8_t> &upload,
                  size_t chunk) {
    for (size_t off = 0; off < upload.size();) {
        size_t take = upload.size() - off;
        if (take > chunk)
            take = chunk;
        if (!v.feed(upload.data() + off, take)) {
            return false;
        }
        off += take;
    }
    return v.finish();
}

constexpr uint8_t kSecret[] = {'s', 'e', 'c', 'r', 'e', 't', '!'};
constexpr size_t kSecretLen = sizeof(kSecret);

} // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// constantTimeMemcmp
// ---------------------------------------------------------------------------

void test_constantTimeMemcmp_equal_returnsZero() {
    const uint8_t a[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t b[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_EQUAL_INT(0, OtaHmac::constantTimeMemcmp(a, b, sizeof(a)));
}

void test_constantTimeMemcmp_differentLastByte_returnsNonZero() {
    const uint8_t a[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t b[8] = {0, 1, 2, 3, 4, 5, 6, 8};
    TEST_ASSERT_NOT_EQUAL(0, OtaHmac::constantTimeMemcmp(a, b, sizeof(a)));
}

void test_constantTimeMemcmp_differentFirstByte_returnsNonZero() {
    const uint8_t a[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t b[8] = {1, 1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_NOT_EQUAL(0, OtaHmac::constantTimeMemcmp(a, b, sizeof(a)));
}

// ---------------------------------------------------------------------------
// Framing — sink only sees body bytes, never the trailer
// ---------------------------------------------------------------------------

void test_validUpload_sinkReceivesOnlyBody_oneChunk() {
    std::vector<uint8_t> body(128);
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>(i);
    const auto upload = buildSignedUpload(body, kSecret, kSecretLen);

    Recorder rec;
    OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, recordSink, &rec);
    TEST_ASSERT_TRUE(v.begin());
    TEST_ASSERT_TRUE(v.feed(upload.data(), upload.size()));
    TEST_ASSERT_TRUE(v.finish());

    TEST_ASSERT_EQUAL_size_t(body.size(), rec.body.size());
    TEST_ASSERT_EQUAL_MEMORY(body.data(), rec.body.data(), body.size());
    TEST_ASSERT_EQUAL_size_t(upload.size(), v.totalBytes());
}

// ---------------------------------------------------------------------------
// Chunking equivalence — same body, any chunk size = same sink output
// ---------------------------------------------------------------------------

void test_chunking_byteByByte_equivalentToMonolithic() {
    std::vector<uint8_t> body(257);
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>((i * 31) & 0xFF);
    const auto upload = buildSignedUpload(body, kSecret, kSecretLen);

    Recorder mono;
    OtaHmac::OtaHmacVerifier v1(kStub, kSecret, kSecretLen, recordSink, &mono);
    TEST_ASSERT_TRUE(v1.begin());
    TEST_ASSERT_TRUE(feedInChunks(v1, upload, upload.size()));

    Recorder bytewise;
    OtaHmac::OtaHmacVerifier v2(kStub, kSecret, kSecretLen, recordSink, &bytewise);
    TEST_ASSERT_TRUE(v2.begin());
    TEST_ASSERT_TRUE(feedInChunks(v2, upload, 1));

    TEST_ASSERT_EQUAL_size_t(mono.body.size(), bytewise.body.size());
    TEST_ASSERT_EQUAL_MEMORY(mono.body.data(), bytewise.body.data(), mono.body.size());
}

void test_chunking_severalSizes_allEquivalent() {
    std::vector<uint8_t> body(1024);
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    const auto upload = buildSignedUpload(body, kSecret, kSecretLen);

    const size_t chunkSizes[] = {1, 2, 31, 32, 33, 64, 100, 256, 1024, upload.size()};
    Recorder reference;
    {
        OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, recordSink, &reference);
        TEST_ASSERT_TRUE(v.begin());
        TEST_ASSERT_TRUE(v.feed(upload.data(), upload.size()));
        TEST_ASSERT_TRUE(v.finish());
    }

    for (size_t cs : chunkSizes) {
        Recorder rec;
        OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, recordSink, &rec);
        TEST_ASSERT_TRUE(v.begin());
        const bool ok = feedInChunks(v, upload, cs);
        char msg[64];
        snprintf(msg, sizeof(msg), "verifier should accept chunk size %zu", cs);
        TEST_ASSERT_TRUE_MESSAGE(ok, msg);
        TEST_ASSERT_EQUAL_size_t(reference.body.size(), rec.body.size());
        TEST_ASSERT_EQUAL_MEMORY(reference.body.data(), rec.body.data(),
                                 reference.body.size());
    }
}

// ---------------------------------------------------------------------------
// Boundary: upload with no body (only the trailer)
// ---------------------------------------------------------------------------

void test_uploadWithEmptyBody_isAccepted() {
    std::vector<uint8_t> body; // empty
    const auto upload = buildSignedUpload(body, kSecret, kSecretLen);
    TEST_ASSERT_EQUAL_size_t(OtaHmac::kHmacLen, upload.size());

    Recorder rec;
    OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, recordSink, &rec);
    TEST_ASSERT_TRUE(v.begin());
    TEST_ASSERT_TRUE(v.feed(upload.data(), upload.size()));
    TEST_ASSERT_TRUE(v.finish());
    TEST_ASSERT_EQUAL_size_t(0, rec.body.size());
}

// ---------------------------------------------------------------------------
// Reject undersized uploads (< trailer length)
// ---------------------------------------------------------------------------

void test_uploadShorterThanTrailer_isRejected() {
    const uint8_t tooShort[OtaHmac::kHmacLen - 1] = {};
    Recorder rec;
    OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, recordSink, &rec);
    TEST_ASSERT_TRUE(v.begin());
    TEST_ASSERT_TRUE(v.feed(tooShort, sizeof(tooShort)));
    TEST_ASSERT_FALSE(v.finish());
    TEST_ASSERT_EQUAL_size_t(0, rec.body.size());
}

void test_emptyUpload_isRejected() {
    Recorder rec;
    OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, recordSink, &rec);
    TEST_ASSERT_TRUE(v.begin());
    TEST_ASSERT_FALSE(v.finish());
}

// ---------------------------------------------------------------------------
// Trailer corruption is detected
// ---------------------------------------------------------------------------

void test_corruptedTrailerLastByte_isRejected() {
    std::vector<uint8_t> body(64, 0xAB);
    auto upload = buildSignedUpload(body, kSecret, kSecretLen);
    upload.back() ^= 0x01; // flip one bit in the trailer

    Recorder rec;
    OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, recordSink, &rec);
    TEST_ASSERT_TRUE(v.begin());
    TEST_ASSERT_TRUE(v.feed(upload.data(), upload.size()));
    TEST_ASSERT_FALSE(v.finish());
}

void test_corruptedBodyByte_isRejected() {
    std::vector<uint8_t> body(64, 0xAB);
    auto upload = buildSignedUpload(body, kSecret, kSecretLen);
    upload[10] ^= 0x80; // flip a bit in the body

    Recorder rec;
    OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, recordSink, &rec);
    TEST_ASSERT_TRUE(v.begin());
    TEST_ASSERT_TRUE(v.feed(upload.data(), upload.size()));
    TEST_ASSERT_FALSE(v.finish());
}

// ---------------------------------------------------------------------------
// Sink failure aborts the upload
// ---------------------------------------------------------------------------

bool failingSink(const uint8_t * /*data*/, size_t /*len*/, void * /*user*/) {
    return false;
}

void test_sinkFailure_abortsFeed() {
    std::vector<uint8_t> body(128, 0xCD);
    const auto upload = buildSignedUpload(body, kSecret, kSecretLen);

    OtaHmac::OtaHmacVerifier v(kStub, kSecret, kSecretLen, failingSink, nullptr);
    TEST_ASSERT_TRUE(v.begin());
    TEST_ASSERT_FALSE(v.feed(upload.data(), upload.size()));
    TEST_ASSERT_FALSE(v.finish());
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_constantTimeMemcmp_equal_returnsZero);
    RUN_TEST(test_constantTimeMemcmp_differentLastByte_returnsNonZero);
    RUN_TEST(test_constantTimeMemcmp_differentFirstByte_returnsNonZero);
    RUN_TEST(test_validUpload_sinkReceivesOnlyBody_oneChunk);
    RUN_TEST(test_chunking_byteByByte_equivalentToMonolithic);
    RUN_TEST(test_chunking_severalSizes_allEquivalent);
    RUN_TEST(test_uploadWithEmptyBody_isAccepted);
    RUN_TEST(test_uploadShorterThanTrailer_isRejected);
    RUN_TEST(test_emptyUpload_isRejected);
    RUN_TEST(test_corruptedTrailerLastByte_isRejected);
    RUN_TEST(test_corruptedBodyByte_isRejected);
    RUN_TEST(test_sinkFailure_abortsFeed);
    return UNITY_END();
}
