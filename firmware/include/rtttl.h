#pragma once
#include <stdint.h>
namespace hackerphone {
struct ToneNote {uint16_t frequency=0,milliseconds=0;};
constexpr unsigned RtttlMaxText=8192,RtttlMaxNotes=1024,RtttlMaxDuration=600000;
struct RtttlTone {char name[25]{};uint16_t count=0;uint32_t duration=0;ToneNote notes[RtttlMaxNotes]{};};
bool parseRtttl(const char* text,RtttlTone& tone);
}
