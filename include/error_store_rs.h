// error_store_rs.h — C ABI for the Rust error-store crate (issue #1177 R-5).
//
// Hand-written. Three exported functions implement the ring-buffer math
// that lives inside `ErrorStore::{push, getAll, dismissAt}`. The C++
// wrapper in `error_store.cpp` holds the portMUX spinlock for the entire
// RMW window — these symbols are called BETWEEN portENTER_CRITICAL and
// portEXIT_CRITICAL, never directly by application code.
//
// Keep this header in sync with `rust/error-store/src/ffi.rs` — both must
// move together in any PR that changes a signature.
//
// `FwError` is defined in `src/diag/error_store.h`. This header doesn't
// pull that in (it lives outside the firmware -I path) — instead it
// forward-declares the struct so the function signatures can take
// `FwError*` opaquely. The Rust crate mirrors the layout in `#[repr(C)]`
// with a compile-time assert (`size_of::<FwError>() == 65`).

#ifndef CANSHIFT_ERROR_STORE_RS_H
#define CANSHIFT_ERROR_STORE_RS_H

#include <stdint.h>

#ifdef __cplusplus
struct FwError;
extern "C" {
#else
struct FwError;
typedef struct FwError FwError;
#endif

// `ErrorStore::push` core. Caller MUST hold the portMUX. Mutates the ring
// + state in place. Duplicate (source, code) updates the existing entry's
// message; otherwise inserts at the next slot or evicts the oldest when
// the ring is full. Increments `*version` unconditionally on any state
// change.
void error_store_push_rs(FwError *ring, uint8_t ring_size, uint8_t *head, uint8_t *count,
                         uint32_t *version, uint8_t source, const char *code, const char *message);

// `ErrorStore::getAll` core. Caller MUST hold the portMUX. Copies up to
// `max_count` entries from the ring into `out` newest-first; returns the
// number written. Does NOT mutate the ring or state.
uint8_t error_store_get_all_rs(const FwError *ring, uint8_t ring_size, uint8_t head, uint8_t count,
                               FwError *out, uint8_t max_count);

// `ErrorStore::dismissAt` core. Caller MUST hold the portMUX. Drops the
// entry at newest-first index `row` and collapses the gap. Out-of-range
// `row` is a silent no-op (version does NOT advance in that case).
void error_store_dismiss_at_rs(FwError *ring, uint8_t ring_size, uint8_t *head, uint8_t *count,
                               uint32_t *version, uint8_t row);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_ERROR_STORE_RS_H
