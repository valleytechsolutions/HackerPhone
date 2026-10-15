#include "saved_lists.h"
#include "radio_service.h"
#include "mp24_board.h"
#include "capture_format.h"
#include <Arduino.h>
namespace hackerphone {
char savedListPath[96]{},savedListMessage[64]="Save or load observations";
static bool directories(){return mp24::cardReady&&(SD.exists("/hackerphone")||SD.mkdir("/hackerphone"))&&(SD.exists("/hackerphone/lists")||SD.mkdir("/hackerphone/lists"));}
static bool path(const char* prefix,const char* extension){if(!directories())return false;for(unsigned i=1;i<100000;++i){snprintf(savedListPath,sizeof(savedListPath),"/hackerphone/lists/%s-%05u.%s",prefix,i,extension);if(!SD.exists(savedListPath))return true;}return false;}
bool saveReconList(unsigned filter){
  if(filter>=unsigned(ScanProfile::Count)||!path("devices","hpl"))return false;File file=SD.open(savedListPath,FILE_WRITE);if(!file)return false;
  bool ok=file.println("HPTARGETS1")>0;unsigned count=0;char line[192];
  for(const auto& t:recon.targets)if(scanMatches(t,ScanProfile(filter))){SavedTarget saved;memcpy(saved.mac,t.mac,6);saved.ble=t.ble;saved.addressType=t.addressType;saved.channel=t.channel;saved.security=t.security;saved.roles=t.accessPoint|(t.station<<1)|(t.probe<<2);snprintf(saved.name,sizeof(saved.name),"%s",t.name);
    size_t n=encodeSavedTarget(line,sizeof(line),saved);if(!n||file.println(line)!=n+2){ok=false;break;}++count;}
  if(ok)ok=file.printf("END %u\n",count)>0;file.flush();file.close();snprintf(savedListMessage,sizeof(savedListMessage),ok?"Saved %u observations":"SD write failed; incomplete file",count);return ok;
}
bool loadReconList(const char* p){
  snprintf(savedListMessage,sizeof(savedListMessage),"List unavailable / invalid path");
  if(!p||strncmp(p,"/hackerphone/",13)||!safeRelativePath(p+13)||!mp24::cardReady||radioRunning)return false;
  File file=SD.open(p,FILE_READ);if(!file||file.isDirectory()||file.size()>16000){file.close();return false;}
  auto* staged=static_cast<SavedTarget*>(ps_calloc(ReconCore::Capacity,sizeof(SavedTarget)));if(!staged){file.close();return false;}
  char line[192];unsigned count=0;bool header=false,ended=false,ok=true;
  while(file.available()){
    size_t n=file.readBytesUntil('\n',line,sizeof(line)-1);if(n&&line[n-1]=='\r')--n;line[n]=0;if(n==sizeof(line)-1){ok=false;break;}
    if(!header){header=!strcmp(line,"HPTARGETS1");if(!header){ok=false;break;}continue;}
    if(!strncmp(line,"END ",4)){char* end;unsigned long expected=strtoul(line+4,&end,10);ended=end!=line+4&&!*end&&expected==count;ok=ended&&!file.available();break;}
    if(count==ReconCore::Capacity||!decodeSavedTarget(line,staged[count])){ok=false;break;}
    for(unsigned i=0;i<count;++i)if(staged[i].ble==staged[count].ble&&staged[i].addressType==staged[count].addressType&&!memcmp(staged[i].mac,staged[count].mac,6))ok=false;
    if(!ok)break;++count;
  }file.close();ok&=header&&ended;
  if(ok){for(auto& t:recon.targets)t={};recon.selected=recon.pinned=-1;uint32_t now=millis();
    for(unsigned i=0;i<count;++i){auto& t=recon.targets[i];auto& s=staged[i];t.used=true;t.ble=s.ble;memcpy(t.mac,s.mac,6);t.addressType=s.addressType;t.channel=s.channel;t.security=s.security;t.accessPoint=s.roles&1;t.station=s.roles&2;t.probe=s.roles&4;memcpy(t.name,s.name,sizeof(t.name));t.first=t.last=now-60000;
      t.match={t.ble?dragon::Family::Ble:dragon::Family::Wifi,dragon::Confidence::None,"Saved observation; waiting for live evidence"};}
    snprintf(savedListPath,sizeof(savedListPath),"%s",p);}
  free(staged);snprintf(savedListMessage,sizeof(savedListMessage),ok?"Loaded %u saved (not live)":"Invalid/incomplete list; unchanged",count);return ok;
}
bool saveSsidList(){
  if(!path("ssids","txt"))return false;File file=SD.open(savedListPath,FILE_WRITE);if(!file)return false;unsigned count=0;bool ok=true;
  for(unsigned i=0;i<ReconCore::Capacity;++i){const auto& t=recon.targets[i];if(!t.used||t.ble||(!t.accessPoint&&!t.probe)||!*t.name||strlen(t.name)>32)continue;
    bool duplicate=false;for(unsigned j=0;j<i;++j){auto& other=recon.targets[j];if(other.used&&!other.ble&&(other.accessPoint||other.probe)&&!strcmp(other.name,t.name))duplicate=true;}if(duplicate)continue;
    if(file.println(t.name)!=strlen(t.name)+2){ok=false;break;}++count;
  }file.flush();file.close();snprintf(savedListMessage,sizeof(savedListMessage),ok?"Saved %u SSIDs":"SSID write failed",count);return ok;
}
}
