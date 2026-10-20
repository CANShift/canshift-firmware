#include "usb_comm_internal.h"

#include "can/bus_scan.h"
#include "can/can_manager.h"
#include "diag/logger.h"
#include "hal/usb/bus_scan_line.h"
#include "util/mem_alloc.h"

#include <new>

namespace {

struct PollScratch {
    BusScanRs report;
    char line[BusScanLine::kLineCap];
};

Mem::HeapPtr<PollScratch> allocScratch() {
    void *block = Mem::allocPreferSpiram(sizeof(PollScratch));
    return Mem::HeapPtr<PollScratch>(block == nullptr ? nullptr : new (block) PollScratch{});
}

}

namespace UsbCommInternal {

void handleBusScanStart() {
    if (!CanManager::isAvailable()) {
        UsbComm::sendError("can_unavailable");
        return;
    }
    if (!BusScan::requestStart()) {
        UsbComm::sendError("bus_scan_busy");
        return;
    }
    LOG_INFO("USB", "Bus scan requested");
    UsbComm::sendOk();
}

void handleBusScanPoll() {
    const Mem::HeapPtr<PollScratch> scratch = allocScratch();
    if (!scratch) {
        LOG_ERROR("USB", "BUS_SCAN_POLL scratch allocation failed");
        UsbComm::sendError("out_of_memory");
        return;
    }
    BusScan::copyReport(scratch->report);
    if (BusScanLine::format(scratch->report, scratch->line, sizeof(scratch->line)) == 0) {
        LOG_ERROR("USB", "BUS_SCAN_POLL reply does not fit %u B",
                  static_cast<unsigned>(sizeof(scratch->line)));
        UsbComm::sendError("bus_scan_too_long");
        return;
    }
    UsbComm::sendLine(scratch->line);
}

}
