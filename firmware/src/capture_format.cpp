#include "capture_format.h"
#include <string.h>
namespace hackerphone {
bool wifiFrameHasAddress(const uint8_t* p,size_t n,const uint8_t address[6]){
  if(!p||!address||n<10||(p[0]&3))return false;unsigned type=(p[0]>>2)&3;
  if(type==1){if(!memcmp(p+4,address,6))return true;return n>=16&&!memcmp(p+10,address,6);}
  if((type!=0&&type!=2)||n<24)return false;
  return !memcmp(p+4,address,6)||!memcmp(p+10,address,6)||!memcmp(p+16,address,6)||(type==2&&(p[1]&3)==3&&n>=30&&!memcmp(p+24,address,6));
}
bool safeRelativePath(const char* p) {
  if(!p||!*p||strlen(p)>120||*p=='/')return false;
  const char* part=p;
  for(const char* c=p;;++c){
    if(*c=='/'||!*c){size_t n=size_t(c-part);if(!n||(n==1&&part[0]=='.')||(n==2&&part[0]=='.'&&part[1]=='.'))return false;if(!*c)break;part=c+1;}
    else if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||*c=='.'||*c=='_'||*c=='-'))return false;
  }return true;
}
size_t csvField(char* out,size_t cap,const char* text) {
  if(!out||!text||cap<3)return 0;
  size_t used=0;
  auto put=[&](char c){if(used+1>=cap)return false;out[used++]=c;return true;};
  put('"');
  const char* start=text;while(*start==' '||*start=='\t'||*start=='\r'||*start=='\n')++start;
  if(*start=='='||*start=='+'||*start=='-'||*start=='@'||*text=='\t'||*text=='\r')if(!put('\'')){out[0]=0;return 0;}
  for(;*text;++text){char c=*text;if(static_cast<unsigned char>(c)<32)c=' ';if(c=='"'&&!put('"')){out[0]=0;return 0;}if(!put(c)){out[0]=0;return 0;}}
  if(!put('"')){out[0]=0;return 0;}out[used]=0;return used;
}
static void le32(uint8_t* out,uint32_t n){for(unsigned i=0;i<4;++i)out[i]=uint8_t(n>>(i*8));}
void pcapHeader(uint8_t out[24]){memset(out,0,24);le32(out,0xa1b2c3d4);out[4]=2;out[6]=4;le32(out+16,1024);le32(out+20,105);}
bool pcapRecord(uint8_t out[16],uint64_t unixMicros,uint32_t captured,uint32_t original){
  if(!captured||captured>1024||captured>original||unixMicros/1000000>0xffffffff)return false;
  le32(out,uint32_t(unixMicros/1000000));le32(out+4,uint32_t(unixMicros%1000000));le32(out+8,captured);le32(out+12,original);return true;
}
}
