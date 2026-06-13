#pragma once

#include <stdint.h>

enum ErrorSource : uint8_t {
    ERROR_SRC_CAN = 0,
    ERROR_SRC_CONFIG = 1,
    ERROR_SRC_SYSTEM = 3,
};

struct FwError {
    ErrorSource source;
    char code[12];
    char message[52];
};

namespace ErrorStore {

void push(ErrorSource source, const char *code, const char *message);

void getAll(FwError *buf, uint8_t *count, uint8_t maxCount);

uint8_t getCount();
uint32_t getVersion();
void dismissLatest();

void dismissAt(uint8_t row);

void clear();

} // namespace ErrorStore
