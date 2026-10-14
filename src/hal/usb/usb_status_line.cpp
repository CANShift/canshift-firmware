#include "usb_status_line.h"

#include "util/json_writer.h"

#include <ArduinoJson.h>

size_t UsbStatusLine::format(const Fields &fields, char *out, size_t cap) {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["version"] = fields.version;
    doc["protocol"] = fields.protocol;
    doc["is_day"] = fields.isDay ? 1 : 0;
    doc["board_id"] = fields.boardId;
    if (doc.overflowed())
        return 0;
    const size_t needed = JsonWriter::measure(doc);
    if (needed >= cap)
        return 0;
    return JsonWriter::toBuffer(doc, out, cap);
}
