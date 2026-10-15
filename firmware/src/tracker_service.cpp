#include "tracker_service.h"
#include "radio_service.h"
#include "ble_explorer.h"
#include "attack_service.h"
#include "lab_tools.h"
#include "network_service.h"
#include <Arduino.h>
namespace hackerphone {
TrackerLiveStatus trackerLive;
static uint8_t attempted[8][7]{};static bool pending=false;
static RadioMode previousMode=RadioMode::Combined;
bool trackerLiveStart(uint16_t seconds){
  auto net=networkStatus();auto g=gattStatus();if(trackerLive.active||seconds<1||seconds>60||attackStatus.active||lab.mode!=LabMode::Off||g.busy||g.connected||net.connected||net.connecting||net.hotspot||net.busy)return false;
  previousMode=radioMode;radioStop();radioMode=RadioMode::Ble;recon.selected=-1;
  if(!radioStart()){radioMode=previousMode;return false;}trackerLive={};trackerLive.active=true;trackerLive.started=millis();trackerLive.seconds=seconds;pending=false;memset(attempted,0,sizeof(attempted));snprintf(trackerLive.message,sizeof(trackerLive.message),"Scanning for Find My / DULT");return true;
}
void trackerLiveStop(){if(!trackerLive.active)return;trackerLive.active=false;if(pending)gattStop();pending=false;radioStop();radioMode=previousMode;snprintf(trackerLive.message,sizeof(trackerLive.message),"Stopped");}
void trackerLiveTick(uint32_t now){
  if(!trackerLive.active)return;
  if(uint32_t(now-trackerLive.started)>=uint32_t(trackerLive.seconds)*1000){trackerLiveStop();return;}
  auto g=gattStatus();if(pending){if(g.busy||g.connected)return;if(g.soundSent)++trackerLive.writes;pending=false;snprintf(trackerLive.message,sizeof(trackerLive.message),"%s",g.message);radioStart();return;}
  if(g.busy||g.connected)return;
  if(trackerLive.attempted>=8)return;
  for(const auto& t:recon.targets){if(!t.ble||!t.fresh(now)||int32_t(t.last-trackerLive.started)<0||t.match.family!=dragon::Family::Tracker)continue;bool seen=false;
    for(unsigned i=0;i<trackerLive.attempted;++i)if(attempted[i][6]==t.addressType&&!memcmp(attempted[i],t.mac,6))seen=true;if(seen)continue;
    if(gattTrackerSound(t.mac,t.addressType)){unsigned i=trackerLive.attempted++;memcpy(attempted[i],t.mac,6);attempted[i][6]=t.addressType;pending=true;snprintf(trackerLive.message,sizeof(trackerLive.message),"Requesting tracker sound");}break;
  }
}
}
