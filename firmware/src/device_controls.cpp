#include "device_controls.h"
#include <string.h>
namespace hackerphone {
PowerEvent PowerButton::update(bool pressed,uint32_t now){
  if(pressed!=raw_){raw_=pressed;changedAt_=now;}
  if(uint32_t(now-changedAt_)>=40&&stable_!=raw_){stable_=raw_;
    if(stable_){downAt_=now;longSent_=false;}
    else if(armed_&&!longSent_){longSent_=true;return PowerEvent::Lock;}}
  if(armed_&&stable_&&!longSent_&&uint32_t(now-downAt_)>=2500){longSent_=true;return PowerEvent::Shutdown;}
  if(!pressed&&!stable_&&uint32_t(now-changedAt_)>=40)armed_=true;
  return PowerEvent::None;
}
uint8_t ledLevel(LedPattern pattern,unsigned led,uint8_t brightness,uint32_t now,uint8_t mask){
  if(led>=8||!(mask&(1u<<led)))return 0;
  switch(pattern){
    case LedPattern::Solid:return brightness;
    case LedPattern::Breathe:{unsigned phase=now%2400;unsigned level=phase<1200?phase:2400-phase;return uint32_t(brightness)*level/1200;}
    case LedPattern::Chase:return (now/150)%8==led?brightness:0;
    default:return 0;
  }
}
bool id3Skip(const uint8_t* h,unsigned n,uint32_t size,uint32_t& out){
  out=0;if(n<3||memcmp(h,"ID3",3))return true;
  if(n<10||h[3]<2||h[3]>4||h[4]==255)return false;
  uint32_t count=0;for(unsigned i=6;i<10;++i){if(h[i]&128)return false;count=(count<<7)|h[i];}
  uint32_t total=count+10+((h[3]==4&&(h[5]&16))?10:0);if(total>size)return false;out=total;return true;
}
bool safeSdPath(const char* p){
 if(!p||*p!='/'||strlen(p)>150||!p[1])return false;const char* part=p+1;
 for(const char* c=part;;++c){if(*c=='/'||!*c){size_t n=c-part;if(!n||(n==1&&part[0]=='.')||(n==2&&part[0]=='.'&&part[1]=='.')||part[n-1]==' ')return false;if(!*c)return true;part=c+1;}
   else if(uint8_t(*c)<32||strchr("\\:*?\"<>|",*c))return false;}
}
uint32_t fileCrc32(uint32_t previous,const uint8_t* bytes,unsigned count){uint32_t crc=~previous;for(unsigned i=0;i<count;++i){crc^=bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}return ~crc;}
}
