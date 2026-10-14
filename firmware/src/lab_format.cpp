#include "lab_tools.h"
#include <string.h>
namespace hackerphone {
size_t makeBeacon(uint8_t* out,size_t cap,const uint8_t mac[6],uint8_t channel,const char* ssid){
  if(!out||!mac||!ssid||channel<1||channel>11)return 0;
  size_t n=strlen(ssid);if(!n||n>32||cap<51+n)return 0;
  memset(out,0,51+n);out[0]=0x80;memset(out+4,255,6);memcpy(out+10,mac,6);memcpy(out+16,mac,6);
  out[32]=100;out[34]=1;out[36]=0;out[37]=n;memcpy(out+38,ssid,n);
  uint8_t rates[]={1,8,0x82,0x84,0x8b,0x96,12,18,24,36,3,1,channel};memcpy(out+38+n,rates,sizeof(rates));return 51+n;
}
bool validLegacyAdvert(const uint8_t* bytes,size_t n){
  if(!bytes||!n||n>31)return false;size_t p=0;
  while(p<n){unsigned z=bytes[p++];if(!z)return p==n;if(z>n-p)return false;p+=z;}return true;
}
}
