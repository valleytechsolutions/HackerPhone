#include "eapol_tracker.h"
#include "attack_frames.h"
#include <string.h>
namespace hackerphone {
bool parseEapolKey(const uint8_t* p,size_t n,EapolKey& out){
  if(!p||n<24||(p[0]&15)!=8||(p[1]&0x40))return false;
  unsigned ds=p[1]&3;if(ds!=1&&ds!=2)return false;
  size_t h=24;if(p[0]&0x80){h+=2;if(p[1]&0x80)h+=4;}
  const uint8_t llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};if(n<h+8+99||memcmp(p+h,llc,8))return false;
  auto k=p+h+8;size_t size=uint16_t(k[2])<<8|k[3];if(k[1]!=3||k[4]!=2||size<95||size>n-h-12)return false;
  unsigned flags=uint16_t(k[5])<<8|k[6];if(!(flags&8)||(flags&0x0c00))return false;
  size_t extra=uint16_t(k[97])<<8|k[98];if(extra>size-95)return false;
  const bool ack=flags&0x80,mic=flags&0x100,install=flags&0x40,secure=flags&0x200;
  EapolKey result;result.message=ack&&!mic?1:!ack&&mic&&!secure?2:ack&&mic&&install?3:!ack&&mic&&secure?4:0;
  if(!result.message||((result.message==1||result.message==3)!=(ds==2)))return false;
  memcpy(result.ap,p+(ds==1?4:10),6);memcpy(result.client,p+(ds==1?10:4),6);
  if(!unicastAddress(result.ap)||!unicastAddress(result.client))return false;
  for(unsigned i=0;i<8;++i)result.replay=(result.replay<<8)|k[9+i];memcpy(result.nonce,k+17,32);
  if(result.message<=3){uint8_t any=0;for(auto c:result.nonce)any|=c;if(!any)return false;}
  // Unencrypted PMKID KDEs only. Encrypted key data must not be interpreted as TLVs.
  if(!(flags&0x1000))for(size_t at=99;at+2<=99+extra;){unsigned len=k[at+1];if(len>99+extra-at-2)break;
    if(k[at]==221&&len==20&&!memcmp(k+at+2,"\x00\x0f\xac\x04",4))result.pmkid=true;at+=len+2;}
  out=result;return true;
}
void EapolTracker::observe(const uint8_t* p,size_t n,uint32_t now){
  EapolKey k;if(!parseEapolKey(p,n,k))return;++messages[k.message-1];if(k.pmkid)++pmkids;
  unsigned slot=0;uint32_t age=0;
  for(unsigned i=0;i<8;++i){auto& s=sessions_[i];if(s.used&&!memcmp(s.ap,k.ap,6)&&!memcmp(s.client,k.client,6)){slot=i;break;}
    if(!s.used){slot=i;break;}if(uint32_t(now-s.at)>=age){slot=i;age=now-s.at;}}
  auto& s=sessions_[slot];bool same=s.used&&!memcmp(s.ap,k.ap,6)&&!memcmp(s.client,k.client,6);
  if(!same||uint32_t(now-s.at)>15000){s={};s.used=true;memcpy(s.ap,k.ap,6);memcpy(s.client,k.client,6);}
  s.at=now;
  if(k.message==1){if(!(s.seen&1)||s.firstReplay!=k.replay||memcmp(s.nonce,k.nonce,32)){s.seen=1;s.counted=false;s.firstReplay=k.replay;memcpy(s.nonce,k.nonce,32);}}
  else if(k.message==2){if((s.seen&1)&&s.firstReplay==k.replay)s.seen|=2;}
  else if(k.message==3){if((s.seen&3)==3&&k.replay>s.firstReplay&&!memcmp(s.nonce,k.nonce,32)){s.thirdReplay=k.replay;s.seen|=4;}}
  else if((s.seen&7)==7&&k.replay==s.thirdReplay)s.seen|=8;
  if(s.seen==15&&!s.counted){s.counted=true;++complete;}
}
}
