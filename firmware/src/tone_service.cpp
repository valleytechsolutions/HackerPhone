#include "tone_service.h"
#include "music_service.h"
#include "mp24_board.h"
#include "device_controls.h"
#include "rtttl.h"
#include <Preferences.h>
namespace hackerphone {
uint8_t toneEnabled=3,toneVolume=80;
static Preferences prefs;static char* tunes[4]{};
// Short synthesized motifs, not original recordings or complete game themes.
static const char* defaults[]={
 "Mario intro:d=8,o=5,b=200:e6,e6,p,e6,p,c6,e6,p,4g6,4p,g,4p",
 "Communicator:d=16,o=8,b=100:c#,p,c#,p,b7,c#,p,e,p,c#,p,p",
 "Recon:d=16,o=6,b=160:c,g,c7,p,c7",
 "Saved:d=16,o=5,b=180:c,e,g,c6"
};
static const char* keys[]={"call","sms","recon","tool"};
void tonesBegin(){prefs.begin("hp-tones",false);toneEnabled=prefs.getUChar("enabled",3);toneVolume=std::min<uint8_t>(100,prefs.getUChar("volume",80));
 for(unsigned i=0;i<4;++i){tunes[i]=static_cast<char*>(ps_calloc(RtttlMaxText+1,1));if(!tunes[i])continue;String s=prefs.getString(keys[i],defaults[i]);RtttlTone t;if(!parseRtttl(s.c_str(),t))s=defaults[i];snprintf(tunes[i],RtttlMaxText+1,"%s",s.c_str());}}
void tonesSave(){prefs.putUChar("enabled",toneEnabled);prefs.putUChar("volume",toneVolume);}
const char* toneText(unsigned kind){return kind<4?(tunes[kind]?tunes[kind]:defaults[kind]):"";}
bool tonePreview(unsigned kind){return kind<4&&musicRtttl(toneText(kind),toneVolume);}
bool toneSet(unsigned kind,const char* text){RtttlTone t;if(kind>=4||!tunes[kind]||!parseRtttl(text,t))return false;if(prefs.putString(keys[kind],text)!=strlen(text))return false;snprintf(tunes[kind],RtttlMaxText+1,"%s",text);return true;}
void toneReset(unsigned kind){if(kind<4)toneSet(kind,defaults[kind]);}
bool toneLoad(unsigned kind,const char* path){if(!mp24::cardReady||!safeSdPath(path)||strncmp(path,"/hackerphone/",13))return false;File file=SD.open(path);if(!file||file.isDirectory()||file.size()>RtttlMaxText){file.close();return false;}size_t wanted=file.size();char* text=static_cast<char*>(ps_malloc(wanted+1));if(!text){file.close();return false;}size_t n=file.readBytes(text,wanted);file.close();bool complete=n==wanted;while(n&&(text[n-1]=='\n'||text[n-1]=='\r'))--n;text[n]=0;bool ok=complete&&toneSet(kind,text);free(text);return ok;}
void tonesTick(const PhoneState& phone,uint32_t now,uint32_t events){
 static uint32_t sms=0,lastEvents=0,ringAt=0,reconAt=0;static bool wasRinging=false;
 bool ringing=phone.call==Call::Ringing;musicCallPause(phone.phonePriority());
 if(wasRinging&&!ringing)musicCancelAlert();
 if(ringing&&(toneEnabled&1)&&!musicStatus().alert&&(!wasRinging||uint32_t(now-ringAt)>=1000)){if(tonePreview(0))ringAt=now;}
 wasRinging=ringing;
 if(phone.inboxNotifications!=sms){sms=phone.inboxNotifications;if((toneEnabled&2)&&!phone.phonePriority())tonePreview(1);}
 if(events!=lastEvents){lastEvents=events;if((toneEnabled&4)&&uint32_t(now-reconAt)>=10000&&!phone.phonePriority()){if(tonePreview(2))reconAt=now;}}
}
bool toneCommand(const char* line){
 unsigned kind=0,value;char extra;bool ok=false;
 if(!strcmp(line,"TONES STATUS")){Serial.printf("HP_TONES enabled=%u level=%u\n",toneEnabled,toneVolume);for(unsigned i=0;i<4;++i)Serial.printf("HP_TONE %u %s\n",i,toneText(i));return true;}
 if(sscanf(line,"TONE PREVIEW %u%c",&kind,&extra)==1)ok=tonePreview(kind);
 else if(sscanf(line,"TONE RESET %u%c",&kind,&extra)==1&&kind<4){toneReset(kind);ok=true;}
 else if(sscanf(line,"TONE VOLUME %u%c",&value,&extra)==1&&value<=100){toneVolume=value;tonesSave();ok=true;}
 else if(sscanf(line,"TONE ENABLE %u%c",&value,&extra)==1&&value<=15){toneEnabled=value;tonesSave();ok=true;}
 else if(!strncmp(line,"TONE SET ",9)||!strncmp(line,"TONE LOAD ",10)){bool load=line[5]=='L';const char* arg=line+(load?10:9);char* end;kind=strtoul(arg,&end,10);if(end!=arg&&*end==' '&&kind<4)ok=load?toneLoad(kind,end+1):toneSet(kind,end+1);}
 else return false;
 Serial.printf("HP_TONE %s\n",ok?"OK":"FAILED");return true;
}
}
