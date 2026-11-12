// Target board pin assignments (docs/Wiring-Diagram.md section 4).
#pragma once

#include <Arduino.h>

#include "OpenGongProto.h"

// Comparator outputs TRIG1..TRIG6 -> MCPWM capture inputs.
// Channels 1-3 use MCPWM group 0, channels 4-6 use group 1. Channel 6 is optional.
constexpr int kTrigPins[og::kMaxChannels] = {2, 4, 5, 6, 7, 47};

// Internal sync line that zeroes both MCPWM capture timers at the same
// instant, so timestamps from the two groups share one time base. The
// firmware drives this pin itself: leave it unconnected.
constexpr int kSyncPin = 48;

// Battery divider enable level. Heltec V3.0/V3.1: LOW. V3.2: HIGH.
constexpr int kVbatCtrlOn = LOW;
constexpr float kVbatDivider = 4.9f;  // 390k / 100k divider on GPIO1
