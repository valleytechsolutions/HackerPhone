#include "radio_service.h"
#include "storage_service.h"
#include "attack_service.h"
#include <Arduino.h>
// Arduino 3.3.12 releases controller RAM before setup unless this registration
// header is linked. NimBLE-Arduino 2.3.6 predates that framework mechanism.
#include <esp32-hal-alloc-ble-mem.h>
#include <NimBLEDevice.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <atomic>
namespace hackerphone {
ReconCore recon;
uint32_t radioDrops=0;
uint8_t radioChannel=1;
bool radioRunning=false,radioBle=false;
bool bleActiveScan=false;
RadioMode radioMode=RadioMode::Combined;
uint8_t channelLock=0;
static NimBLEScan* scan=nullptr;
static QueueHandle_t readyQueue,freeQueue;
static std::atomic<uint32_t> dropped{0};
static std::atomic<uint32_t> generation{0};
static std::atomic<bool> accepting{false};
static uint32_t phaseAt=0,hopAt=0;
static bool networkReady=false,wifiReady=false;
struct Packet {uint32_t at,generation;uint64_t micros;uint16_t size,original;uint8_t mac[6],type,channel;int8_t rssi;bool ble;uint8_t bytes[1024];};
static Packet* pool=nullptr;
struct CachedBeacon {uint32_t at;uint16_t length;uint8_t mac[6],bytes[1024];};
static CachedBeacon* beacons=nullptr;
size_t radioBeacon(const uint8_t mac[6],uint8_t* out,size_t cap){
  if(!beacons||!mac)return 0;for(unsigned i=0;i<16;++i){auto& b=beacons[i];
    if(b.length&&uint32_t(millis()-b.at)<60000&&!memcmp(b.mac,mac,6)){
      if(out){if(cap<b.length)return 0;memcpy(out,b.bytes,b.length);}return b.length;}}
  return 0;
}
static void cacheBeacon(const Packet& p){
  if(!beacons||p.size!=p.original||p.size<36||p.bytes[0]!=0x80)return;
  unsigned slot=0;uint32_t oldest=0;
  for(unsigned i=0;i<16;++i){auto& b=beacons[i];if(!b.length||!memcmp(b.mac,p.bytes+10,6)){slot=i;break;}
    uint32_t age=p.at-b.at;if(age>=oldest){oldest=age;slot=i;}}
  auto& b=beacons[slot];b.at=p.at;b.length=p.size;memcpy(b.mac,p.bytes+10,6);memcpy(b.bytes,p.bytes,p.size);
}
static bool reserve(uint8_t& i){if(freeQueue&&xQueueReceive(freeQueue,&i,0)==pdTRUE)return true;++dropped;return false;}
static void publish(uint8_t i){if(!accepting.load()||pool[i].generation!=generation.load()){xQueueSend(freeQueue,&i,0);return;}if(xQueueSend(readyQueue,&i,0)!=pdTRUE){++dropped;xQueueSend(freeQueue,&i,0);}}
static void wifiRx(void* data,wifi_promiscuous_pkt_type_t type){
  uint32_t current=generation.load();if(!accepting.load())return;
  if(type!=WIFI_PKT_MGMT&&type!=WIFI_PKT_DATA&&type!=WIFI_PKT_CTRL)return;
  auto* p=static_cast<wifi_promiscuous_pkt_t*>(data);
  if(p->rx_ctrl.rx_state||p->rx_ctrl.sig_len<(type==WIFI_PKT_CTRL?14:28))return;
  uint8_t i;if(!reserve(i))return;auto& out=pool[i];out.generation=current;out.at=millis();out.micros=esp_timer_get_time();out.original=p->rx_ctrl.sig_len-4;out.size=std::min<size_t>(out.original,sizeof(out.bytes));
  out.ble=false;out.channel=p->rx_ctrl.channel;out.rssi=p->rx_ctrl.rssi;memcpy(out.bytes,p->payload,out.size);publish(i);
}
class Callbacks:public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d)override{
    uint32_t current=generation.load();if(!accepting.load())return;
    const auto& bytes=d->getPayload();if(bytes.size()>255)return;
    uint8_t i;if(!reserve(i))return;auto& out=pool[i];out.generation=current;out.at=millis();out.ble=true;out.size=bytes.size();out.rssi=d->getRSSI();
    auto addr=d->getAddress();out.type=addr.getType();auto* mac=addr.getBase()->val;for(unsigned k=0;k<6;++k)out.mac[k]=mac[5-k];
    memcpy(out.bytes,bytes.data(),bytes.size());publish(i);
  }
};
static Callbacks callbacks;
bool radioPrepare(){
  if(!beacons){beacons=static_cast<CachedBeacon*>(ps_calloc(16,sizeof(CachedBeacon)));if(!beacons)return false;}
  if(!pool){
    pool=static_cast<Packet*>(ps_malloc(sizeof(Packet)*48));if(!pool)return false;
    readyQueue=xQueueCreate(48,1);freeQueue=xQueueCreate(48,1);
    if(!readyQueue||!freeQueue){
      if(readyQueue)vQueueDelete(readyQueue);if(freeQueue)vQueueDelete(freeQueue);
      readyQueue=freeQueue=nullptr;free(pool);pool=nullptr;return false;
    }
    for(uint8_t i=0;i<48;++i)xQueueSend(freeQueue,&i,0);
  }
  if(!networkReady){
    if(esp_netif_init()!=ESP_OK)return false;
    esp_err_t e=esp_event_loop_create_default();
    if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    networkReady=true;
  }
  if(!wifiReady){
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    if(esp_wifi_init(&cfg)!=ESP_OK)return false;
    if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||esp_wifi_set_mode(WIFI_MODE_NULL)!=ESP_OK||esp_wifi_start()!=ESP_OK){esp_wifi_deinit();return false;}
    wifiReady=true;
    wifi_country_t country={"US",1,11,20,WIFI_COUNTRY_POLICY_MANUAL};esp_wifi_set_country(&country);
    wifi_promiscuous_filter_t filter{WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA|WIFI_PROMIS_FILTER_MASK_CTRL};
    esp_wifi_set_promiscuous_filter(&filter);esp_wifi_set_promiscuous_rx_cb(wifiRx);
    wifi_promiscuous_filter_t controls{WIFI_PROMIS_CTRL_FILTER_MASK_ALL};esp_wifi_set_promiscuous_ctrl_filter(&controls);
  }
  if(!scan){
    if(!NimBLEDevice::init(""))return false;
    scan=NimBLEDevice::getScan();if(!scan)return false;scan->setScanCallbacks(&callbacks,true);
    NimBLEDevice::setSecurityAuth(false,false,true);
    scan->setActiveScan(bleActiveScan);scan->setInterval(100);scan->setWindow(85);scan->setMaxResults(0);scan->setDuplicateFilter(false);
  }
  return scan!=nullptr;
}
bool radioStart(){
  if(radioRunning)return true;if(!radioPrepare())return false;
  if(esp_wifi_set_channel(radioChannel,WIFI_SECOND_CHAN_NONE)!=ESP_OK||esp_wifi_set_promiscuous(true)!=ESP_OK)return false;
  phaseAt=hopAt=millis();radioBle=false;radioRunning=true;accepting=true;return true;
}
void radioStop(){accepting=false;++generation;if(scan)scan->stop();if(wifiReady)esp_wifi_set_promiscuous(false);radioRunning=false;
  if(readyQueue){uint8_t i;while(xQueueReceive(readyQueue,&i,0)==pdTRUE)xQueueSend(freeQueue,&i,0);}}
void radioSetMode(RadioMode mode){bool running=radioRunning;radioStop();radioMode=mode;recon.selected=-1;if(running)radioStart();}
void radioSetBleActive(bool active){bool running=radioRunning;radioStop();bleActiveScan=active;if(scan)scan->setActiveScan(active);if(running)radioStart();}
void radioTick(uint32_t now){
  if(!pool)return;
  static uint32_t sampledAt=now;uint32_t elapsed=now-sampledAt;sampledAt=now;
  if(radioRunning&&!radioBle&&radioChannel>=1&&radioChannel<=11)recon.channelMillis[radioChannel]+=std::min<uint32_t>(elapsed,1000);
  const uint64_t start=esp_timer_get_time();uint8_t i;
  while(xQueueReceive(readyQueue,&i,0)==pdTRUE){
    if(!radioRunning||pool[i].generation!=generation.load()){xQueueSend(freeQueue,&i,0);continue;}
    auto& p=pool[i];if(p.ble)recon.ble(p.bytes,p.size,p.mac,p.type,p.rssi,p.at);else{
      capturePacket(p.bytes,p.size,p.original,p.micros);
      // Keep truncated records in PCAP, but do not classify partial management IEs.
      if(p.size==p.original||(p.bytes[0]&12)==8){int target=recon.wifi(p.bytes,p.size,p.rssi,p.channel,p.at);if(target>=0)cacheBeacon(p);}
      attackObserve(p.bytes,p.size,p.at);
    }
    xQueueSend(freeQueue,&i,0);if(esp_timer_get_time()-start>2500)break;
  }
  radioDrops=dropped.load();if(!radioRunning)return;
  int selected=recon.selected;
  if(selected>=0&&recon.targets[selected].fresh(now)){
    auto& t=recon.targets[selected];
    if(t.ble){if(!radioBle){esp_wifi_set_promiscuous(false);radioBle=true;}if(!scan->isScanning())scan->start(10000,false,true);return;}
    if(radioBle){scan->stop();radioBle=false;}
    if(t.channel>=1&&t.channel<=11){radioChannel=t.channel;esp_wifi_set_channel(radioChannel,WIFI_SECOND_CHAN_NONE);esp_wifi_set_promiscuous(true);return;}
  }
  bool wantBle=radioBle;
  if(radioMode==RadioMode::Ble)wantBle=true;else if(radioMode==RadioMode::Wifi)wantBle=false;
  else if(uint32_t(now-phaseAt)>=3300){phaseAt=now;wantBle=!radioBle;}
  if(wantBle!=radioBle){radioBle=wantBle;if(radioBle)esp_wifi_set_promiscuous(false);else{scan->stop();esp_wifi_set_promiscuous(true);}}
  if(radioBle){if(!scan->isScanning())scan->start(3300,false,true);}
  else if(channelLock>=1&&channelLock<=11){if(radioChannel!=channelLock){radioChannel=channelLock;esp_wifi_set_channel(radioChannel,WIFI_SECOND_CHAN_NONE);}}
  else if(uint32_t(now-hopAt)>=280){hopAt=now;radioChannel=radioChannel%11+1;esp_wifi_set_channel(radioChannel,WIFI_SECOND_CHAN_NONE);}
}
}
