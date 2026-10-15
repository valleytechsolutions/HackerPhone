#pragma once
#include <stdint.h>
namespace hackerphone {
struct MusicStatus {
 bool busy=false,playing=false,paused=false,repeat=false,ready=false,alert=false;
 uint8_t volume=35,channels=0;uint32_t rate=0,frames=0,position=0,size=0,elapsedMs=0,errors=0,stackFree=0;
 char path[160]{},message[64]="Choose an MP3 on SD";
};
void musicBegin(uint8_t volume,bool repeat);
bool musicPlay(const char* path);
bool musicTone();
bool musicRtttl(const char* text,uint8_t level=100);
void musicCallPause(bool pause);
void musicCancelAlert();
void musicStop();
void musicPause();
void musicVolume(int volume);
void musicRepeat(bool repeat);
bool musicNext(int direction);
void musicTick();
MusicStatus musicStatus();
}
