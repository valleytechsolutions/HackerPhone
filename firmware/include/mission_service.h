#pragma once
#include <stdint.h>
namespace hackerphone {
struct MissionStatus {bool active=false,error=false;uint32_t started=0,records=0,observed=0,relationships=0,probes=0,alerts=0,bytes=0;char path[96]{},message[64]="Ready to record a mission";};
extern MissionStatus mission;
bool missionStart(unsigned radio);
void missionStop();
void missionTick(uint32_t now);
}
