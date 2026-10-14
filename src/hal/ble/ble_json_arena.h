#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

namespace BleJsonArena {

enum class Slot : uint8_t {
    Status,
    Callback,
};

class LeasedDocument {
  public:
    explicit LeasedDocument(Slot slot);
    ~LeasedDocument();

    LeasedDocument(const LeasedDocument &) = delete;
    LeasedDocument &operator=(const LeasedDocument &) = delete;

    [[nodiscard]] bool held() const {
        return held_;
    }
    JsonDocument &document() {
        return doc_;
    }

  private:
    Slot slot_;
    bool held_;
    JsonDocument doc_;
};

} // namespace BleJsonArena
