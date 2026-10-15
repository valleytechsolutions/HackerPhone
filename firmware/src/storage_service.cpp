#include "storage_service.h"
#include "mp24_board.h"
#include "vendor_db.h"
#include "capture_format.h"
#include "detection.h"
namespace hackerphone {
StorageStatus storageStatus;
static File capture;
static uint8_t* buffer=nullptr;
static constexpr size_t Capacity=65536;
static size_t head=0,tail=0,used=0;
static bool eapol=false;
static bool filtered=false;static uint8_t targetAddress[6]{};
static uint32_t flushed=0;
class SdReader:public VendorReader {
 public:File file;
 size_t size()const override{return const_cast<File&>(file).size();}
 bool read(size_t offset,void* out,size_t n)override{return file&&file.seek(offset)&&file.read(static_cast<uint8_t*>(out),n)==int(n);}
};
static SdReader vendorFile;static VendorDb vendors;
struct Cache{uint32_t prefix=0;bool used=false,found=false;char name[48]{};};
static Cache cache[16];static unsigned cacheNext=0;
void storageBegin(){
  if(!mp24::cardReady)return;
  vendorFile.file=SD.open("/hackerphone/oui.bin",FILE_READ);
  storageStatus.vendorReady=vendorFile.file&&vendors.open(vendorFile);storageStatus.vendors=vendors.count();
}
bool lookupVendor(const uint8_t mac[6],bool ble,uint8_t type,char out[48]){
  out[0]=0;if(!storageStatus.vendorReady||(ble?type!=0:bool(mac[0]&3)))return false;
  uint32_t prefix=uint32_t(mac[0])<<16|uint32_t(mac[1])<<8|mac[2];
  for(auto& c:cache)if(c.used&&c.prefix==prefix){memcpy(out,c.name,48);return c.found;}
  auto& c=cache[cacheNext++%16];c={};c.used=true;c.prefix=prefix;c.found=vendors.lookup(mac,ble,type,c.name);memcpy(out,c.name,48);return c.found;
}
static bool drain(){
  if(!used)return true;
  size_t n=std::min<size_t>(4096,std::min(used,Capacity-tail));
  if(capture.write(buffer+tail,n)!=n){storageStatus.error=true;storageStatus.recording=false;capture.close();used=0;return false;}
  storageStatus.bytes+=n;tail=(tail+n)%Capacity;used-=n;return true;
}
bool captureStart(bool only,const uint8_t* address){
  if(storageStatus.recording)return true;if(!mp24::cardReady)return false;
  if(!buffer)buffer=static_cast<uint8_t*>(ps_malloc(Capacity));if(!buffer)return false;
  if(!SD.exists("/hackerphone")&&!SD.mkdir("/hackerphone"))return false;
  for(unsigned n=1;n<100000;++n){snprintf(storageStatus.path,sizeof(storageStatus.path),"/hackerphone/capture-%05u.pcap",n);if(!SD.exists(storageStatus.path))break;if(n==99999)return false;}
  capture=SD.open(storageStatus.path,FILE_WRITE);if(!capture)return false;
  uint8_t h[24];pcapHeader(h);if(capture.write(h,sizeof(h))!=sizeof(h)){capture.close();storageStatus.error=true;return false;}
  head=tail=used=0;eapol=only;flushed=millis();storageStatus.records=storageStatus.drops=0;storageStatus.bytes=24;storageStatus.error=false;storageStatus.recording=true;
  filtered=address!=nullptr;if(address)memcpy(targetAddress,address,6);
  return true;
}
void captureStop(){if(!capture)return;storageStatus.recording=false;while(used&&drain()){}if(capture){capture.flush();capture.close();}}
static void append(const uint8_t* p,size_t n){while(n){size_t z=std::min(n,Capacity-head);memcpy(buffer+head,p,z);head=(head+z)%Capacity;used+=z;p+=z;n-=z;}}
void capturePacket(const uint8_t* data,size_t n,size_t original,uint64_t micros){
  if(!storageStatus.recording)return;
  if(filtered&&!wifiFrameHasAddress(data,n,targetAddress))return;
  if(eapol&&!dragon::parseWifi(data,n).eapol)return;
  uint8_t h[16];if(!pcapRecord(h,micros,n,original))return;
  if(Capacity-used<n+16){++storageStatus.drops;return;}append(h,16);append(data,n);++storageStatus.records;
}
void storageTick(uint32_t now){if(!capture)return;if(used)drain();if(capture&&uint32_t(now-flushed)>2000){flushed=now;capture.flush();}}
}
