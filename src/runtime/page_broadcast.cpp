#include "page_broadcast.h"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

namespace PageBroadcast {

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Snapshot s_latest = {{}, 0, kNoCurrentPage};

}

void publish(const Snapshot &snapshot) {
    portENTER_CRITICAL(&s_mux);
    s_latest = snapshot;
    portEXIT_CRITICAL(&s_mux);
}

Snapshot latest() {
    portENTER_CRITICAL(&s_mux);
    const Snapshot copy = s_latest;
    portEXIT_CRITICAL(&s_mux);
    return copy;
}

const char *currentId(const Snapshot &snapshot) {
    if (snapshot.current >= snapshot.count)
        return "";
    return snapshot.ids[snapshot.current];
}

void writePageList(JsonObject out, const Snapshot &snapshot) {
    JsonArray pages = out["pages"].to<JsonArray>();
    const uint8_t count = snapshot.count < kConfigMaxPages ? snapshot.count : kConfigMaxPages;
    for (uint8_t i = 0; i < count; ++i)
        pages.add(static_cast<const char *>(snapshot.ids[i]));
}

}
