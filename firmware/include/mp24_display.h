#pragma once
#include <stdint.h>
namespace mp24 {
bool displayBegin();
bool displayPresent(const void* rgb565BigEndian);
extern uint32_t displayFrames,displayErrors;
}
