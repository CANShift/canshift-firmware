#include "format_float.h"
#include "format_float_rs.h"

#include <stdint.h>

namespace FloatFormat {

size_t formatFixed(char *buf, size_t size, float value, int decimals) {
    if (buf == nullptr || size == 0)
        return 0;
    return format_fixed_rs(buf, size, value, decimals);
}

size_t formatFromSpec(char *buf, size_t size, float value, const char *spec) {
    if (buf == nullptr || size == 0)
        return 0;
    return format_from_spec_rs(buf, size, value, spec);
}

size_t formatGeneral(char *buf, size_t size, float value, int sigDigits) {
    if (buf == nullptr || size == 0)
        return 0;
    return format_general_rs(buf, size, value, sigDigits);
}

} // namespace FloatFormat
