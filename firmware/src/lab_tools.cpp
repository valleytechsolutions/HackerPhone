#include "lab_tools.h"
#include "radio_service.h"
#include "ble_explorer.h"
#include "attack_service.h"
#include "network_service.h"
#include "tracker_service.h"
#include <Arduino.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
namespace hackerphone {
LabStatus lab;
static uint8_t frame[96];static size_t frameLength=0;static uint32_t lastTx=0;
bool labStart(LabMode mode,uint8_t channel,const uint8_t* advert,size_t n,const uint8_t* mac,uint8_t addressType){
  if(mode==LabMode::Off||gattStatus().connected||gattStatus().busy||trackerLive.active)return false;
  auto net=networkStatus();if(net.connected||net.connecting||net.hotspot||net.busy)return false;
  bool replay=mode==LabMode::BleReplay||mode==LabMode::BleIdentityReplay;
  if(replay&&!validLegacyAdvert(advert,n))return false;
  if(mode==LabMode::BleIdentityReplay&&(!mac||addressType!=1||(mac[0]>>6)==2))return false;
  attackStop();
  if(!radioPrepare())return false;radioStop();labStop();
  lab={};lab.started=millis();lastTx=lab.started;
  if(mode==LabMode::WifiBeacon){
    uint8_t mac[6];
    if(esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK||esp_wifi_get_mac(WIFI_IF_STA,mac)!=ESP_OK||esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE)!=ESP_OK){snprintf(lab.message,sizeof(lab.message),"Wi-Fi setup failed");esp_wifi_set_mode(WIFI_MODE_NULL);return false;}
    frameLength=makeBeacon(frame,sizeof(frame),mac,channel,"HackerPhone-Lab");if(!frameLength){esp_wifi_set_mode(WIFI_MODE_NULL);return false;}
  }else{
    auto* a=NimBLEDevice::getAdvertising();a->reset();a->setConnectableMode(BLE_GAP_CONN_MODE_NON);a->setAdvertisingInterval(160);a->enableScanResponse(false);
    NimBLEAdvertisementData data;
    if(replay){if(!data.addData(advert,n))return false;}
    else{data.setFlags(6);data.setName("HackerPhone-Lab");}
    if(mode==LabMode::BleIdentityReplay){char address[18];snprintf(address,sizeof(address),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
      if(!NimBLEDevice::setOwnAddr(NimBLEAddress(address,BLE_ADDR_RANDOM)))return false;NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);}
    if(!a->setAdvertisementData(data)||!a->start(15000)){NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);snprintf(lab.message,sizeof(lab.message),"Advertising failed");return false;}
    lab.accepted=1;
  }
  lab.mode=mode;snprintf(lab.message,sizeof(lab.message),"15-second session active");return true;
}
void labStop(){
  if(lab.mode==LabMode::WifiBeacon)esp_wifi_set_mode(WIFI_MODE_NULL);
  else if(lab.mode==LabMode::BleBeacon||lab.mode==LabMode::BleReplay||lab.mode==LabMode::BleIdentityReplay){NimBLEDevice::getAdvertising()->stop();NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);}
  lab.mode=LabMode::Off;snprintf(lab.message,sizeof(lab.message),"Stopped");
}
void labTick(uint32_t now){
  if(lab.mode==LabMode::Off)return;
  if(uint32_t(now-lab.started)>=15000){labStop();return;}
  if(lab.mode==LabMode::WifiBeacon&&uint32_t(now-lastTx)>=100){lastTx=now;uint64_t timestamp=uint64_t(now)*1000;for(unsigned i=0;i<8;++i)frame[24+i]=timestamp>>(8*i);
    if(esp_wifi_80211_tx(WIFI_IF_STA,frame,frameLength,true)==ESP_OK)++lab.accepted;else{++lab.errors;labStop();snprintf(lab.message,sizeof(lab.message),"Driver rejected frame");}}
}
}
