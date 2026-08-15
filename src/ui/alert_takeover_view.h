#pragma once

#include <lvgl.h>

namespace AlertTakeoverView {

void build(lv_event_cb_t ackCb);

bool isBuilt();

void setHidden(bool hidden);

void setSignalName(const char *text);

void setValue(const char *text);

void setContext(const char *text);

} // namespace AlertTakeoverView
