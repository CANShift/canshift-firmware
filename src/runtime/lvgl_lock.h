#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t g_lvglMutex;

class LvglLock {
  public:
    explicit LvglLock(TickType_t timeout)
        : m_held(xSemaphoreTake(g_lvglMutex, timeout) == pdTRUE) {}
    ~LvglLock() {
        if (m_held) {
            xSemaphoreGive(g_lvglMutex);
        }
    }

    LvglLock(const LvglLock &) = delete;
    LvglLock &operator=(const LvglLock &) = delete;
    LvglLock(LvglLock &&) = delete;
    LvglLock &operator=(LvglLock &&) = delete;

    [[nodiscard]] bool held() const {
        return m_held;
    }

  private:
    const bool m_held;
};
