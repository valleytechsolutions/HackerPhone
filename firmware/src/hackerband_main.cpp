// Compile-only C3 foundation. No assumed display/RTC/button/buzzer wiring.
#include <Arduino.h>
#include "band_core.h"

static hackerband::Clock watchClock;
static hackerband::Stopwatch stopwatch;
static hackerband::IncomingCall incoming;
static hackerband::MatrixRain rain;

void setup(){
  Serial.begin(115200);
  Serial.println("HB_CORE 0.1 compile-only; hardware adapter not established");
}
void loop(){
  const uint32_t now=millis();
  watchClock.tick(now);stopwatch.tick(now);incoming.tick(now);rain.tick(now);
  delay(10);
}
