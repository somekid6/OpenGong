// Hardware timestamping of the comparator TRIG lines with the ESP32-S3 MCPWM
// capture units (System-Design section 6).
#pragma once

#include <Arduino.h>

#include "OpenGongProto.h"

// One trigger event: the first rising edge on each channel after arming.
struct RawEvent {
  uint32_t ticks[og::kMaxChannels];  // capture timer counts (both groups synced)
  uint8_t  mask;                     // channels that fired
};

// Configures the capture channels and starts the capture task.
bool captureBegin(const int* pins, int nch, int syncPin);

// Next completed event from the capture task, if any.
bool captureNext(RawEvent& ev, TickType_t wait);

// Capture timer frequency (80 MHz on the ESP32-S3 = 12.5 ns per tick).
uint32_t captureResolutionHz();

// All rising edges seen per channel while armed (for the "stats" command).
void captureEdgeCounts(uint32_t out[og::kMaxChannels]);

bool captureArmed();

// Channels whose first edge is within windowTicks of the earliest channel.
uint8_t captureWindowMask(const RawEvent& ev, int nch, uint32_t windowTicks);
