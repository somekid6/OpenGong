// Turns raw capture events into solved shots, and runs calibration.
#pragma once

#include <Arduino.h>

#include "capture.h"

struct ShotView {
  uint16_t shot = 0;
  bool     fix = false;
  float    dx = 0, dy = 0;  // mm, shooter's view, relative to the aim point
  float    rms_ns = 0;
  uint8_t  used = 0, fired = 0, flags = 0;
  uint32_t at_ms = 0;
};

void processorBegin(uint16_t bootId);
void processEvent(const RawEvent& ev, uint8_t extraFlags);

bool processorLastShot(ShotView& out);
uint16_t processorShotCount();
uint16_t processorRejectCount();
bool processorCalActive();

// Console hooks
void processorSimulate(float x, float y, float noiseNs, Print& out);
void processorCheck(bool on, float x, float y, Print& out);
void calStart(bool backFace, Print& out);
void calAt(float x, float y, Print& out);
void calUndo(Print& out);
void calList(Print& out);
void calFit(bool fitSpeed, Print& out);
void calApply(Print& out);
void calStop(Print& out);
