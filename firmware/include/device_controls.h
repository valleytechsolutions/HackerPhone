#pragma once
#include <stdint.h>
namespace hackerphone {
enum class PowerEvent:uint8_t {None,Lock,Shutdown};
// Release-to-toggle prevents a long hold from briefly locking/unlocking first.
class PowerButton {
 public:
  PowerEvent update(bool pressed,uint32_t now);
  uint32_t held(uint32_t now)const{return stable_?uint32_t(now-downAt_):0;}
 private:
  bool raw_=false,stable_=false,longSent_=false,armed_=false;
  uint32_t changedAt_=0,downAt_=0;
};
enum class LedPattern:uint8_t {Off,Solid,Breathe,Chase,Count};
uint8_t ledLevel(LedPattern pattern,unsigned led,uint8_t brightness,uint32_t now,uint8_t mask);
// MP3 ID3v2 header skip: strict syncsafe size and bounded against file length.
bool id3Skip(const uint8_t* header,unsigned length,uint32_t fileSize,uint32_t& offset);
bool safeSdPath(const char* path);
uint32_t fileCrc32(uint32_t previous,const uint8_t* bytes,unsigned count);
}
