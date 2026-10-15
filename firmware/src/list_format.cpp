#include "saved_lists.h"
#include "attack_frames.h"
#include <string.h>
#include <stdio.h>
namespace hackerphone {
size_t encodeSavedTarget(char* out,size_t cap,const SavedTarget& t){
  if(!out||(!t.ble&&!unicastAddress(t.mac))||t.addressType>1||t.channel>11||t.roles>7||t.security>7||!memchr(t.name,0,64))return 0;
  char line[192];int n=snprintf(line,sizeof(line),"%c %02X%02X%02X%02X%02X%02X %u %u %u %u ",t.ble?'B':'W',t.mac[0],t.mac[1],t.mac[2],t.mac[3],t.mac[4],t.mac[5],t.addressType,t.channel,t.roles,t.security);
  const char* hex="0123456789ABCDEF";if(!*t.name)line[n++]='-';else for(auto* p=reinterpret_cast<const uint8_t*>(t.name);*p;++p){line[n++]=hex[*p>>4];line[n++]=hex[*p&15];}
  line[n]=0;if(cap<=size_t(n))return 0;memcpy(out,line,n+1);return n;
}
bool decodeSavedTarget(const char* line,SavedTarget& out){
  if(!line||strlen(line)>=192)return false;char buffer[192];strcpy(buffer,line);char* fields[7];unsigned count=0;char* at=buffer;
  for(;;){if(count==7)return false;fields[count++]=at;char* space=strchr(at,' ');if(!space)break;*space=0;at=space+1;}
  if(count!=7||strlen(fields[0])!=1||(fields[0][0]!='B'&&fields[0][0]!='W'))return false;
  SavedTarget t;t.ble=fields[0][0]=='B';if(!parseMac(fields[1],t.mac,!t.ble))return false;
  unsigned values[4]{};for(unsigned i=0;i<4;++i){auto p=fields[2+i];if(!*p)return false;for(;*p;++p){if(*p<'0'||*p>'9')return false;values[i]=values[i]*10+(*p-'0');if(values[i]>11)return false;}}
  if(values[0]>1||values[1]>11||values[2]>7||values[3]>7||(!t.ble&&values[0]))return false;
  t.addressType=values[0];t.channel=values[1];t.roles=values[2];t.security=values[3];
  if(strcmp(fields[6],"-")){size_t n=strlen(fields[6]);if(!n||(n&1)||n>126)return false;
    auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;};
    for(unsigned i=0;i<n;i+=2){int a=digit(fields[6][i]),b=digit(fields[6][i+1]);if(a<0||b<0)return false;uint8_t c=(a<<4)|b;if(c<32||c>126)return false;t.name[i/2]=c;}}
  out=t;return true;
}
}
