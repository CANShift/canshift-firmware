#pragma once

#include <stdlib.h>

inline void heap_caps_free(void *ptr) {
    free(ptr);
}
