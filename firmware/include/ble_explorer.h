#pragma once
#include <stdint.h>
#include <stddef.h>
namespace hackerphone {
struct GattRow{uint16_t handle=0;uint8_t flags=0;char service[37]{},uuid[37]{};};
struct GattStatus {bool connected=false,busy=false,truncated=false;uint8_t rows=0;uint32_t notifications=0;char message[48]="Choose a BLE device";uint16_t valueHandle=0;uint8_t value[48]{},valueSize=0;};
bool gattConnect(const uint8_t mac[6],uint8_t type);
void gattStop();
bool gattRead(uint16_t handle);
bool gattWrite(uint16_t handle,const uint8_t* data,size_t n,bool response=true);
bool gattSubscribe(uint16_t handle,bool enable);
GattStatus gattStatus();
bool gattRow(unsigned index,GattRow& row);
}
