#pragma once
// error_store.h — Thread-safe ring buffer of active firmware errors.
//
// Push from any task (CAN, USB, config loader, boot).
// Read from the LVGL UI task via ErrorBar::update().
// Uses a portMUX spinlock — critical sections are copy-only, no I/O.

#include <stdint.h>

enum ErrorSource : uint8_t {
    ERROR_SRC_CAN    = 0, // TWAI / CAN bus faults
    ERROR_SRC_CONFIG = 1, // Config read / parse failures
    ERROR_SRC_SYSTEM = 3, // Boot / system faults
};

struct FwError {
    ErrorSource source;
    char        code[12];    // Short code: "TWAI_ERR", "PARSE_ERR", etc.
    char        message[52]; // Human-readable detail
};

namespace ErrorStore {

// Push a new error, or update in-place if source+code already present.
void push(ErrorSource source, const char *code, const char *message);

// Copy up to maxCount errors into buf (newest first). Sets *count.
void getAll(FwError *buf, uint8_t *count, uint8_t maxCount);

uint8_t  getCount();
uint32_t getVersion(); // Increments on every push/dismiss/clear — use to detect changes.
void     dismissLatest();
void     clear();

} // namespace ErrorStore
