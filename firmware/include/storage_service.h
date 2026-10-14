#pragma once
#include <stdint.h>
#include <stddef.h>
namespace hackerphone {
struct StorageStatus {bool recording=false,error=false,vendorReady=false;uint32_t records=0,drops=0,bytes=0,vendors=0;char path[80]{};};
extern StorageStatus storageStatus;
void storageBegin();
bool captureStart(bool eapolOnly=false);
void captureStop();
void capturePacket(const uint8_t* data,size_t n,size_t original,uint64_t micros);
void storageTick(uint32_t now);
bool lookupVendor(const uint8_t mac[6],bool ble,uint8_t type,char out[48]);
}
