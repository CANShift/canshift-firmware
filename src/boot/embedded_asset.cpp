#include "embedded_asset.h"

#include "hal/storage/storage_driver.h"

#include <memory>
#include <stdlib.h>
#include <string.h>

#if CONFIG_IDF_TARGET_ESP32S3
    #include "esp32s3/rom/miniz.h"
#else
    #include "esp32/rom/miniz.h"
#endif

namespace {

constexpr size_t kLengthHeaderBytes = sizeof(uint32_t);
constexpr uint8_t kZlibWindowBitsBias = 8;
constexpr uint8_t kWindowBits = 12;
constexpr size_t kWindowBytes = size_t{1} << kWindowBits;
constexpr int kInflateFlags = TINFL_FLAG_PARSE_ZLIB_HEADER;

struct FreeDeleter {
    void operator()(void *block) const {
        free(block);
    }
};

template <typename T>
using HeapPtr = std::unique_ptr<T, FreeDeleter>;

template <typename T>
HeapPtr<T> heapAlloc(size_t bytes) {
    return HeapPtr<T>(static_cast<T *>(malloc(bytes)));
}

class ChunkedWriteSession {
  public:
    explicit ChunkedWriteSession(const char *path)
        : open_(StorageDriver::beginChunkedWriteAtomic(path)) {}
    ~ChunkedWriteSession() {
        if (open_) {
            StorageDriver::abortChunkedWrite();
        }
    }
    ChunkedWriteSession(const ChunkedWriteSession &) = delete;
    ChunkedWriteSession &operator=(const ChunkedWriteSession &) = delete;

    bool isOpen() const {
        return open_;
    }
    bool commit() {
        open_ = false;
        return StorageDriver::endChunkedWrite();
    }

  private:
    bool open_;
};

struct DeflatedInput {
    const uint8_t *next;
    size_t remaining;
};

uint32_t readInflatedLength(const uint8_t *start) {
    uint32_t length = 0;
    memcpy(&length, start, sizeof(length));
    return length;
}

bool fitsWindow(const DeflatedInput &input) {
    return (input.next[0] >> 4) + kZlibWindowBitsBias <= kWindowBits;
}

enum class Step : uint8_t { More, Done, Corrupt, WriteFailed };

Step classify(tinfl_status status) {
    if (status == TINFL_STATUS_DONE) {
        return Step::Done;
    }
    return status == TINFL_STATUS_HAS_MORE_OUTPUT ? Step::More : Step::Corrupt;
}

Step inflateStep(tinfl_decompressor &decompressor, DeflatedInput &input, uint8_t *window,
                 size_t &windowPos, size_t &produced) {
    size_t consumed = input.remaining;
    size_t emitted = kWindowBytes - windowPos;
    const tinfl_status status = tinfl_decompress(&decompressor, input.next, &consumed, window,
                                                 window + windowPos, &emitted, kInflateFlags);
    input.next += consumed;
    input.remaining -= consumed;
    const Step step = classify(status);
    if (step == Step::Corrupt) {
        return step;
    }
    if (emitted > 0 && !StorageDriver::appendChunk(window + windowPos, emitted)) {
        return Step::WriteFailed;
    }
    windowPos = (windowPos + emitted) & (kWindowBytes - 1);
    produced += emitted;
    return step;
}

constexpr EmbeddedAsset::Outcome kStepOutcomes[] = {
    EmbeddedAsset::Outcome::Corrupt,
    EmbeddedAsset::Outcome::Written,
    EmbeddedAsset::Outcome::Corrupt,
    EmbeddedAsset::Outcome::WriteFailed,
};

EmbeddedAsset::Outcome inflateStream(tinfl_decompressor &decompressor, DeflatedInput input,
                                     uint8_t *window, size_t expectedLength) {
    tinfl_init(&decompressor);
    size_t windowPos = 0;
    size_t produced = 0;
    Step step = Step::More;
    while (step == Step::More && produced <= expectedLength) {
        step = inflateStep(decompressor, input, window, windowPos, produced);
    }
    if (step == Step::Done && produced != expectedLength) {
        return EmbeddedAsset::Outcome::Corrupt;
    }
    return kStepOutcomes[static_cast<size_t>(step)];
}

constexpr size_t kZlibTrailerBytes = sizeof(uint32_t);

} // namespace

bool EmbeddedAsset::contentStamp(const uint8_t *start, const uint8_t *end, uint32_t &stamp) {
    const size_t blobLength = static_cast<size_t>(end - start);
    if (blobLength <= kLengthHeaderBytes + kZlibTrailerBytes)
        return false;
    const uint8_t *trailer = end - kZlibTrailerBytes;
    stamp = (static_cast<uint32_t>(trailer[0]) << 24) | (static_cast<uint32_t>(trailer[1]) << 16) |
            (static_cast<uint32_t>(trailer[2]) << 8) | static_cast<uint32_t>(trailer[3]);
    return true;
}

EmbeddedAsset::Outcome EmbeddedAsset::inflateToFile(const char *path, const uint8_t *start,
                                                    const uint8_t *end, size_t &inflatedLength) {
    const size_t blobLength = static_cast<size_t>(end - start);
    if (blobLength <= kLengthHeaderBytes) {
        return Outcome::Empty;
    }
    inflatedLength = readInflatedLength(start);
    const DeflatedInput input = {start + kLengthHeaderBytes, blobLength - kLengthHeaderBytes};
    if (!fitsWindow(input)) {
        return Outcome::Corrupt;
    }
    HeapPtr<uint8_t> window = heapAlloc<uint8_t>(kWindowBytes);
    HeapPtr<tinfl_decompressor> decompressor =
        heapAlloc<tinfl_decompressor>(sizeof(tinfl_decompressor));
    if (!window || !decompressor) {
        return Outcome::NoMemory;
    }
    ChunkedWriteSession session(path);
    if (!session.isOpen()) {
        return Outcome::WriteFailed;
    }
    const Outcome outcome = inflateStream(*decompressor, input, window.get(), inflatedLength);
    if (outcome != Outcome::Written) {
        return outcome;
    }
    return session.commit() ? Outcome::Written : Outcome::WriteFailed;
}
