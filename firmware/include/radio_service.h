#pragma once
#include "recon_core.h"
namespace hackerphone {
extern ReconCore recon;
extern uint32_t radioDrops;
extern uint8_t radioChannel;
extern bool radioRunning,radioBle;
enum class RadioMode:uint8_t {Combined,Wifi,Ble};
extern RadioMode radioMode;
extern uint8_t channelLock;
void radioSetMode(RadioMode mode);
bool radioPrepare();
bool radioStart();
void radioStop();
void radioTick(uint32_t now);
}
