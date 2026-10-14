#include "vendor_db.h"
#include <string.h>
namespace hackerphone {
static uint32_t le32(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
const char* addressKind(const uint8_t mac[6],bool ble,uint8_t type){
  if(!ble)return mac[0]&2?"Local Wi-Fi address":"Global Wi-Fi address";
  if(type==0)return "Public BLE address";
  if(type!=1)return "Resolved/unknown address";
  switch(mac[0]>>6){case 3:return "Random static BLE";case 1:return "Private rotating BLE";case 0:return "Private nonresolvable";default:return "Reserved BLE address";}
}
bool VendorDb::open(VendorReader& reader){
  reader_=nullptr;count_=0;uint8_t h[16];
  if(!reader.read(0,h,sizeof(h))||memcmp(h,"HPOUI1\0\0",8)||le32(h+12)!=RecordSize)return false;
  uint32_t n=le32(h+8);if(!n||n>100000||reader.size()!=HeaderSize+size_t(n)*RecordSize)return false;
  reader_=&reader;count_=n;return true;
}
bool VendorDb::lookup(const uint8_t mac[6],bool ble,uint8_t type,char out[48]){
  out[0]=0;if(!reader_||!mac||(ble?type!=0:bool(mac[0]&3)))return false;
  uint32_t key=uint32_t(mac[0])<<16|uint32_t(mac[1])<<8|mac[2],lo=0,hi=count_;
  while(lo<hi){uint32_t mid=lo+(hi-lo)/2;uint8_t r[RecordSize];
    if(!reader_->read(HeaderSize+size_t(mid)*RecordSize,r,sizeof(r)))return false;
    uint32_t prefix=le32(r);if(prefix>0xffffff)return false;
    if(prefix<key)lo=mid+1;else if(prefix>key)hi=mid;else{
      const void* end=memchr(r+4,0,48);if(!end||!r[4])return false;
      size_t n=static_cast<const uint8_t*>(end)-(r+4);for(size_t i=0;i<n;++i)if(r[4+i]<32||r[4+i]>126)return false;
      memcpy(out,r+4,n+1);return true;
    }
  }return false;
}
}
