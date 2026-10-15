#pragma once
#include <stdint.h>
namespace hackerphone {
struct ToneNote {uint16_t frequency=0,milliseconds=0;};
struct RtttlTone {char name[25]{};uint16_t count=0;uint32_t duration=0;ToneNote notes[128]{};};
bool parseRtttl(const char* text,RtttlTone& tone);
}
