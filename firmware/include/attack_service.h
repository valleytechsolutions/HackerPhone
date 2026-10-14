#pragma once
#include "attack_frames.h"

namespace hackerphone {
struct AttackOptions {AttackTarget target;uint16_t durationSeconds=15,packetsPerSecond=10;};
struct AttackStatus {
  bool active=false;AttackMode mode=AttackMode::Beacon;
  uint32_t started=0,accepted=0,errors=0;int lastError=0;
  char message[64]="Idle";
};
extern AttackOptions attackOptions;
extern AttackStatus attackStatus;
bool attackStart(AttackMode mode);
void attackStop();
void attackTick(uint32_t now);
bool attackSelectAp(unsigned index);
bool attackSelectClient(unsigned index);
bool attackAvailable(AttackMode mode,char* why,size_t capacity);
// Local capability marker only; this does not establish off-air delivery.
bool rawFrameAdapterLinked();
}
