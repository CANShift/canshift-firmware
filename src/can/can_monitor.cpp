#include "can_monitor.h"

#include "can_monitor_rs.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "util/mem_alloc.h"

#include <freertos/FreeRTOS.h>

#include <atomic>
#include <new>

namespace {

std::atomic<CanMonitorRs *> s_monitor{nullptr};
std::atomic<bool> s_active{false};
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

CanMonitorRs *monitorState() {
    return s_monitor.load(std::memory_order_acquire);
}

bool ensureMonitorState() {
    if (monitorState() != nullptr)
        return true;
    void *block = Mem::allocPreferSpiram(sizeof(CanMonitorRs));
    if (block == nullptr) {
        LOG_ERROR("CAN", "CAN monitor state allocation failed");
        ErrorStore::push(ERROR_SRC_CAN, "MON_ALLOC", "CAN monitor state allocation failed");
        return false;
    }
    s_monitor.store(new (block) CanMonitorRs{}, std::memory_order_release);
    return true;
}

}

bool CanMonitor::start() {
    if (!ensureMonitorState())
        return false;
    s_active.store(true, std::memory_order_release);
    LOG_INFO("CAN", "CAN monitor started");
    return true;
}

void CanMonitor::stop() {
    CanMonitorRs *monitor = monitorState();
    const bool wasActive = s_active.exchange(false, std::memory_order_acq_rel);
    if (monitor == nullptr)
        return;
    portENTER_CRITICAL(&s_mux);
    can_monitor_reset_rs(monitor);
    portEXIT_CRITICAL(&s_mux);
    if (wasActive)
        LOG_INFO("CAN", "CAN monitor stopped");
}

bool CanMonitor::active() {
    return s_active.load(std::memory_order_acquire);
}

void CanMonitor::offer(uint32_t id, bool extended, const uint8_t *data, uint8_t len,
                       uint32_t nowMs) {
    if (!active())
        return;
    const uint32_t taggedId = extended ? (id | kCanMonitorExtendedId) : id;
    portENTER_CRITICAL(&s_mux);
    can_monitor_offer_rs(monitorState(), taggedId, data, len, nowMs);
    portEXIT_CRITICAL(&s_mux);
}

bool CanMonitor::setFilter(const uint8_t *data, size_t len) {
    if (!ensureMonitorState())
        return false;
    portENTER_CRITICAL(&s_mux);
    const bool applied = can_monitor_set_filter_rs(monitorState(), data, len);
    portEXIT_CRITICAL(&s_mux);
    return applied;
}

size_t CanMonitor::encode(uint32_t nowMs, uint8_t *out, size_t cap) {
    if (!active())
        return 0;
    portENTER_CRITICAL(&s_mux);
    const size_t len = can_monitor_encode_rs(monitorState(), nowMs, out, cap);
    portEXIT_CRITICAL(&s_mux);
    return len;
}
