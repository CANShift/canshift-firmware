# ErrorStore

Source: [`src/diag/error_store.cpp`](../../src/diag/error_store.cpp)

ErrorStore is the firmware's single source of truth for "what went wrong"
across boot, CAN, USB, BLE, config, and the UI alert chain. It's read by
the ErrorBar overlay and the BLE STATUS characteristic.
Sources: `src/diag/error_store.cpp`, `src/diag/error_store.h`,
`include/error_store_rs.h`.

## Critical-section invariant

All public functions hold a `portMUX_TYPE` spinlock for the entire
read-modify-write window. The lock is `portENTER_CRITICAL` /
`portEXIT_CRITICAL` — on ESP32 the IDF implementation is a spinlock plus
IRQ-disable on the current core.

**Inside any `portENTER_CRITICAL` block in this file you MUST NOT**:

- call `LOG_*` (can block / take another mutex / alloc)
- allocate (`new`/`delete`/`malloc`/`free`, `String`, `snprintf` to heap)
- take any other lock (mutex / semaphore / `lv_lock`)

Violations deadlock or trip the IDF crit-section assert at runtime.
`strncpy` / `memcpy` on fixed-size buffers is fine — no heap.

Issue #877 fixed the original crash that surfaced when `LOG_WARN`
inside a critical section deadlocked against the logger's UART mutex.

## Ring buffer math + Rust port

The push / get-all / dismiss-at math has been ported to Rust under
`rust/error-store/` (#1177 R-5). The C++ wrapper still owns:

- The portMUX spinlock (`s_mux`) — Rust can't manage it without a HAL
  binding for FreeRTOS critical sections.
- The static buffer + state globals (`s_ring`, `s_head`, `s_count`,
  `s_version`) — they live in `.bss`, exposed to Rust through raw
  pointers under the lock.
- The trivial accessors (`getCount`, `getVersion`, `dismissLatest`,
  `clear`) — one-liner state mutations that don't justify an FFI
  boundary.

The Rust crate sees the ring with exclusive access guarantees by
construction (the C++ wrapper holds the lock for the entire RMW window
including the call into Rust). The Rust impl does not know about the
lock and does not try to acquire one.

`FwError` is 65 bytes (1-byte `source` + 12-byte `code` + 52-byte
`message`, no padding). The Rust port has a compile-time
`assert!(core::mem::size_of::<FwError>() == 65)` to guard against C-side
layout drift.

## Ring sizing — `ERROR_STORE_RING_SIZE = 6`

Raising the depth widens `FwError[]` copies inside critical sections —
keep `≤16` (F-ME-12). 6 is enough that a transient burst (failed CAN
init + dropped USB cmd + stale signal warning) doesn't evict the
underlying root cause.

Promoted from a local constexpr so the Studio ErrorBar UI and any other
cross-package consumer can stay in sync with a single source of truth.

## Push semantics

`ErrorStore::push(source, code, message)`:

- Duplicate `(source, code)` → updates the existing entry's message in
  place. No eviction, no ordering change. `version` still increments so
  subscribers see the change.
- New `(source, code)` and ring not full → inserts at the next slot
  (`head + count`).
- New `(source, code)` and ring full → overwrites the **oldest** entry
  (head) and advances head by one. Newest entry always at the tail.

`version` is incremented unconditionally on any state change so
`getVersion()` polls cheaply (no need to diff the buffer).

## getAll — newest-first

`ErrorStore::getAll(buf, &count, maxCount)` copies up to `maxCount`
entries into `buf`, **newest first**. The ErrorBar reads with
`maxCount = MAX_ROWS = 6` so the cap matches the ring size.

## dismissAt — collapse the gap

`ErrorStore::dismissAt(row)` drops the entry at newest-first index
`row`:

- `row == 0` (newest) → just decrement count, no shift needed.
- `row == count - 1` (oldest) → advance head by one, no copy needed.
- middle → shift entries one position toward head to close the gap.
  The newest entry's prior slot is left untouched but unreferenced.

Out-of-range `row` is a silent no-op AND does NOT advance version —
locked down by `test_dismissAt_versionAdvancesOnSuccess_…` in the
Unity suite.

## Version bumps that the UI reads

`ErrorBar::update()` polls `ErrorStore::getVersion()` once per UI frame
and short-circuits when unchanged. This is the bandwidth budget that
keeps the error-render path off the hot frame loop. Adding a code path
that bumps version without changing the buffer would burn UI cycles
re-rendering the same content.
