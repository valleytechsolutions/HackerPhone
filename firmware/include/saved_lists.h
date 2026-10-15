#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hackerphone {
struct SavedTarget {uint8_t mac[6]{},addressType=0,channel=0,roles=0,security=0;bool ble=false;char name[64]{};};
size_t encodeSavedTarget(char* out,size_t capacity,const SavedTarget& target);
bool decodeSavedTarget(const char* line,SavedTarget& target);
// SD application layer; saved observations are never restored as live devices.
bool saveReconList(unsigned filter);
bool loadReconList(const char* path);
bool saveSsidList();
extern char savedListPath[96],savedListMessage[64];
}
