// Text command console, used over USB serial and relayed over LoRa from the receiver.
#pragma once

#include <Arduino.h>

void consoleExec(char* line, Print& out);
void consolePollSerial();
bool consoleRebootRequested();
