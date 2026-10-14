#pragma once
#include "ir_format.h"
namespace hackerphone {
enum class IrState {Idle,Learning,Ready,Sending,Error};
struct IrStatus {
  IrState state=IrState::Idle;
  bool unsaved=false,library=false;
  unsigned record=0;
  uint32_t learned=0,sent=0;
  char message[80]="Choose Record or SD library";
  char path[192]{};
};
extern IrStatus irStatus;
const IrSignal* irSignal();
bool irDirectories();
bool irLearn();
void irStop();
void irTick(uint32_t now);
bool irSend();
bool irSave(const char* name=nullptr);
bool irOpen(const char* path);
bool irNext();
bool irPrevious();
bool irSetFrequency(uint32_t frequency);
uint32_t irFrequency();
bool irBusy();
bool irStorageTest(); // Dedicated synthetic SD fixture; never transmits.
}
