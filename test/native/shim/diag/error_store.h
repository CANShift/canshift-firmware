#pragma once
// diag/error_store.h shim — forwards to the real header now that the native
// env compiles `src/diag/error_store.cpp` against the freertos portmacro
// shim. Kept as a separate file so the include path layout stays uniform
// across host tests; remove once every native consumer pulls the canonical
// header directly.

#include "../../../../src/diag/error_store.h"
