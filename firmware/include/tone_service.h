#pragma once
#include "phone_core.h"
namespace hackerphone {
extern uint8_t toneEnabled,toneVolume;
void tonesBegin();
void tonesSave();
void tonesTick(const PhoneState& phone,uint32_t now,uint32_t reconEvents);
bool tonePreview(unsigned kind);
bool toneSet(unsigned kind,const char* rtttl);
bool toneLoad(unsigned kind,const char* path);
const char* toneText(unsigned kind);
void toneReset(unsigned kind);
bool toneCommand(const char* line);
}
