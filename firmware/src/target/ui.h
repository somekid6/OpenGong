// Target OLED status screen, LED and battery reading.
#pragma once

#include <stdint.h>

void uiBegin();
void uiUpdate();      // call from loop(); redraws a few times per second
void uiShotEvent();   // a shot was just processed
uint16_t uiBatteryMv();
