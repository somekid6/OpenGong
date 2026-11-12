#include "ui.h"

#include <Arduino.h>
#include <SSD1306Wire.h>
#include <Wire.h>

#include "board.h"
#include "capture.h"
#include "config.h"
#include "link.h"
#include "processor.h"

namespace {

using namespace og::heltec;

SSD1306Wire s_oled(0x3c, kOledSda, kOledScl, GEOMETRY_128_64);
bool s_dirty = true;
uint32_t s_lastDraw = 0;
uint32_t s_ledOffAt = 0;
uint16_t s_battMv = 0;
uint32_t s_lastBatt = 0;

uint16_t readBattery() {
  pinMode(kVbatCtrl, OUTPUT);
  digitalWrite(kVbatCtrl, kVbatCtrlOn);
  delay(2);
  const uint32_t mv = analogReadMilliVolts(kVbatAdc);
  pinMode(kVbatCtrl, INPUT);
  return static_cast<uint16_t>(mv * kVbatDivider);
}

void draw() {
  s_oled.clear();
  s_oled.setTextAlignment(TEXT_ALIGN_LEFT);
  s_oled.setFont(ArialMT_Plain_10);

  char line[40];
  snprintf(line, sizeof line, "OpenGong%s  #%u", processorCalActive() ? " CAL" : "", processorShotCount());
  s_oled.drawString(0, 0, line);
  s_oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  s_oled.drawString(128, 0, captureArmed() ? "ARMED" : "hold");
  s_oled.setTextAlignment(TEXT_ALIGN_LEFT);

  ShotView v;
  if (processorLastShot(v)) {
    s_oled.setFont(ArialMT_Plain_16);
    if (v.fix) snprintf(line, sizeof line, "%+.1f  %+.1f", v.dx, v.dy);
    else snprintf(line, sizeof line, "no fix");
    s_oled.drawString(0, 13, line);
    s_oled.setFont(ArialMT_Plain_10);
    char mask[8] = {};
    for (int i = 0; i < g_activeCh; ++i) mask[i] = (v.fired & (1u << i)) ? '1' + i : '-';
    snprintf(line, sizeof line, "rms %.0fns  ch %s%s", v.rms_ns, mask, v.flags & og::SHOT_HIGH_RESID ? " !" : "");
    s_oled.drawString(0, 32, line);
  } else {
    s_oled.drawString(0, 18, "waiting for first hit");
  }

  const LinkStats ls = linkStats();
  if (ls.up) snprintf(line, sizeof line, "LoRa %lu/%lu ack", (unsigned long)ls.shotsAcked, (unsigned long)ls.shotsSent);
  else snprintf(line, sizeof line, "LoRa DOWN");
  s_oled.drawString(0, 44, line);
  snprintf(line, sizeof line, "%.2fV", s_battMv / 1000.0f);
  s_oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  s_oled.drawString(128, 44, line);
  s_oled.display();
}

}  // namespace

void uiBegin() {
  pinMode(kLed, OUTPUT);
  digitalWrite(kLed, LOW);
  pinMode(kVext, OUTPUT);
  digitalWrite(kVext, LOW);  // power the OLED
  delay(50);
  pinMode(kOledRst, OUTPUT);
  digitalWrite(kOledRst, LOW);
  delay(20);
  digitalWrite(kOledRst, HIGH);
  delay(20);
  s_oled.init();
  s_oled.flipScreenVertically();
  s_battMv = readBattery();
  s_lastBatt = millis();
  draw();
}

void uiShotEvent() {
  s_dirty = true;
  digitalWrite(kLed, HIGH);
  s_ledOffAt = millis() + 150;
}

uint16_t uiBatteryMv() { return s_battMv; }

void uiUpdate() {
  const uint32_t now = millis();
  if (s_ledOffAt && static_cast<int32_t>(now - s_ledOffAt) >= 0) {
    digitalWrite(kLed, LOW);
    s_ledOffAt = 0;
  }
  if (now - s_lastBatt > 30000) {
    s_battMv = readBattery();
    s_lastBatt = now;
  }
  // Redraw on a shot, and every 500 ms for the ARMED/hold and link status.
  if (s_dirty || now - s_lastDraw > 500) {
    draw();
    s_dirty = false;
    s_lastDraw = now;
  }
}
