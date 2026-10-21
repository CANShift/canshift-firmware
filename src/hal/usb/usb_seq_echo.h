#pragma once

#include <stddef.h>
#include <stdint.h>

namespace UsbSeqEcho {

constexpr size_t kMaxTaggedLine = 256;
constexpr size_t kSeqSuffixRoom = sizeof(",\"seq\":4294967295}") - 1;

struct Seq {
    bool present;
    uint32_t value;
};

[[nodiscard]] Seq parseAfterCmd(const char *afterCmd);

[[nodiscard]] size_t closeReply(Seq seq, char *out, size_t cap);

[[nodiscard]] size_t tag(const char *line, size_t len, Seq seq, char *out, size_t cap);

class Scope {
  public:
    explicit Scope(Seq seq);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    bool armed_;
};

[[nodiscard]] Seq active();

}
