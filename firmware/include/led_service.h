#pragma once
#include "device_controls.h"
namespace hackerphone {
struct LedSettings {LedPattern pattern=LedPattern::Off;uint8_t brightness=80,mask=255,notifications=15;bool ready=false;};
extern LedSettings ledSettings;
void ledsStart();
void ledsSave();
void ledsNotify(unsigned event,uint32_t now);
void ledsTick(uint32_t now,bool locked=false,bool shutdown=false);
void ledsTest(uint32_t now);
}
