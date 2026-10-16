#pragma once
#include "phone_core.h"
namespace hackerphone {
struct VoiceStatus {bool active=false,busy=false;uint32_t frames=0,errors=0;uint16_t downPeak=0,micLeft=0,micRight=0,otherPeak=0;int error=0;unsigned stage=0;};
void voiceTick(const PhoneState& phone);
void voiceStop();
bool voiceTest(const PhoneState& phone,uint32_t now);
VoiceStatus voiceStatus();
}
