#include "can_rx_route.h"

#include "can_parser.h"
#include "obd2_dtc.h"
#include "obd2_poller.h"

namespace {

bool consumedByObd2(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (Obd2Dtc::onRxFrame(frameId, data, length))
        return true;
    return Obd2Poller::onRxFrame(frameId, data, length);
}

} // namespace

void CanRxRoute::deliver(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (!consumedByObd2(frameId, data, length))
        CanParser::parseFrame(frameId, data, length);
}
