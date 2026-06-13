#pragma once

#include <stdint.h>

namespace SettingsPage {

void init(int16_t yOffset, int16_t height);

void open();

void close();

bool toggle();

bool isOpen();

uint32_t lastOpenMs();

int16_t getOpenY();
int16_t getClosedY();
int16_t getPanelHeight();
void setPanelY(int16_t y);
void snapOpen();
void snapClosed();

bool isDragging();
void setDragging(bool dragging);

uint8_t getBrightness();

bool getBleEnabled();

void applyFromUsb(uint8_t brightness);

} // namespace SettingsPage
