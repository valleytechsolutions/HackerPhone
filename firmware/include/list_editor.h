#pragma once
#include <stdint.h>
#include <stddef.h>
namespace hackerphone {
extern char editorMessage[80],editorPath[160];
unsigned ssidCount();
const char* ssidName(unsigned index);
bool ssidAdd(const char* text);
bool ssidRemove(unsigned index);
bool ssidGenerate(unsigned count);
void ssidClear();
bool ssidLoad(const char* path);
bool ssidSave();
bool targetAddAp(const uint8_t mac[6],uint8_t channel,const char* ssid);
bool targetAddStation(const uint8_t mac[6],unsigned ap);
bool targetsJsonSave();
bool targetsJsonLoad(const char* path);
bool targetRemove(unsigned index);
bool radioMac(bool ap,const uint8_t* address); // nullptr restores factory MAC
bool radioRandomMac(bool ap);
unsigned markedAttackTargets();
bool editorCommand(const char* line);
}
