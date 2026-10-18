#include "json_writer.h"

#include <string.h>

namespace {

class CountingPrint : public Print {
  public:
    size_t write(uint8_t) override {
        return 1;
    }
    size_t write(const uint8_t *, size_t size) override {
        return size;
    }
};

class BufferPrint : public Print {
  public:
    BufferPrint(char *buffer, size_t capacity) : cursor_(buffer), end_(buffer + capacity) {}

    size_t write(uint8_t c) override {
        return write(&c, 1);
    }
    size_t write(const uint8_t *data, size_t size) override {
        const size_t room = static_cast<size_t>(end_ - cursor_);
        const size_t copied = size < room ? size : room;
        memcpy(cursor_, data, copied);
        cursor_ += copied;
        return copied;
    }

  private:
    char *cursor_;
    char *end_;
};

}

size_t JsonWriter::toPrint(JsonVariantConst value, Print &out) {
    return serializeJson(value, out);
}

size_t JsonWriter::measure(JsonVariantConst value) {
    CountingPrint counter;
    return toPrint(value, counter);
}

size_t JsonWriter::toBuffer(JsonVariantConst value, char *buffer, size_t capacity) {
    BufferPrint out(buffer, capacity);
    const size_t written = toPrint(value, out);
    if (written < capacity) {
        buffer[written] = '\0';
    }
    return written;
}
