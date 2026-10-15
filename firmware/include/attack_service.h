#pragma once
#include "attack_frames.h"

namespace hackerphone {
struct AttackOptions {AttackTarget target;uint16_t durationSeconds=15,packetsPerSecond=10;bool useList=false;char ssidPath[96]="/hackerphone/ssids.txt";};
struct AttackStatus {
  bool active=false;AttackMode mode=AttackMode::Beacon;
  uint32_t started=0,accepted=0,errors=0;int lastError=0;
  uint32_t challenges=0,responses=0;
  char message[64]="Idle";
};
extern AttackOptions attackOptions;
extern AttackStatus attackStatus;
bool attackStart(AttackMode mode);
void attackStop();
void attackTick(uint32_t now);
void attackObserve(const uint8_t* frame,size_t size,uint32_t now);
bool attackSelectAp(unsigned index);
bool attackSelectClient(unsigned index);
bool attackAvailable(AttackMode mode,char* why,size_t capacity);
bool attackListAdd();
void attackListClear();
unsigned attackListCount();
bool attackListGet(unsigned index,AttackTarget& target);
bool attackListRemove(unsigned index);
unsigned attackListObservedClients();
// Local capability marker only; this does not establish off-air delivery.
bool rawFrameAdapterLinked();
}
