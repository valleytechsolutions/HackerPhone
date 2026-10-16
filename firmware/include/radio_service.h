#pragma once
#include "recon_core.h"
namespace hackerphone {
extern ReconCore recon;
extern uint32_t radioDrops;
extern uint8_t radioChannel;
extern bool radioRunning,radioBle;
extern bool bleActiveScan;
enum class RadioMode:uint8_t {Combined,Wifi,Ble};
extern RadioMode radioMode;
extern uint8_t channelLock;
void radioSetMode(RadioMode mode);
void radioSetBleActive(bool active);
bool radioPrepare(bool withBle=true);
bool radioStart();
void radioStop();
void radioTick(uint32_t now);
size_t radioBeacon(const uint8_t mac[6],uint8_t* output,size_t capacity);
}
