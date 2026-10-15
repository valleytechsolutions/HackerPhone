#pragma once
#include <stdint.h>
namespace hackerphone {
struct TrackerLiveStatus {bool active=false;uint32_t started=0;uint16_t seconds=15;uint8_t attempted=0,writes=0;char message[64]="Idle";};
extern TrackerLiveStatus trackerLive;
bool trackerLiveStart(uint16_t seconds=15);
void trackerLiveStop();
void trackerLiveTick(uint32_t now);
}
