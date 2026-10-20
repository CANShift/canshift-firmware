
#pragma once

#include <stddef.h>

namespace FloatFormat {

size_t formatFixed(char *buf, size_t size, float value, int decimals);

size_t formatFromSpec(char *buf, size_t size, float value, const char *spec);

size_t formatFromSpecWithDecimals(char *buf, size_t size, float value, const char *spec,
                                  int decimals);

int specDecimals(const char *spec);

size_t formatGeneral(char *buf, size_t size, float value, int sigDigits);

}
