#include "led_service.h"
#include "mp24_board.h"
#include <Preferences.h>
namespace hackerphone {
LedSettings ledSettings;static Preferences prefs;
static uint8_t previous[8]{255,255,255,255,255,255,255,255};
static uint32_t last=0,alertAt=0,testAt=0;static unsigned eventId=0;static bool alert=false,testing=false;
void ledsStart(){prefs.begin("hp-leds",false);ledSettings.pattern=LedPattern(prefs.getUChar("pattern",0)%unsigned(LedPattern::Count));ledSettings.brightness=prefs.getUChar("level",80);ledSettings.mask=prefs.getUChar("mask",255);ledSettings.notifications=prefs.getUChar("notify",15);ledSettings.ready=mp24::ledsBegin();}
void ledsSave(){prefs.putUChar("pattern",unsigned(ledSettings.pattern));prefs.putUChar("level",ledSettings.brightness);prefs.putUChar("mask",ledSettings.mask);prefs.putUChar("notify",ledSettings.notifications);}
void ledsNotify(unsigned event,uint32_t now){if(event<4&&(ledSettings.notifications&(1u<<event))&&(!alert||uint32_t(now-alertAt)>=3000)){eventId=event;alertAt=now;alert=true;}}
void ledsTest(uint32_t now){testAt=now;testing=true;}
void ledsTick(uint32_t now,bool locked,bool shutdown){
 if(!ledSettings.ready||(!shutdown&&uint32_t(now-last)<50))return;last=now;
 if(uint32_t(now-alertAt)>=5000)alert=false;if(uint32_t(now-testAt)>=5000)testing=false;
 for(unsigned i=0;i<8;++i){uint8_t v=0;
   if(!shutdown){if(testing)v=ledLevel(LedPattern::Chase,i,ledSettings.brightness,now-testAt,255);
     else if(alert){unsigned t=(now-alertAt)%1500;bool pulse=t/250<=eventId&&(t%250)<120;v=pulse&&((ledSettings.mask>>i)&1)?ledSettings.brightness:0;}
     else if(!locked)v=ledLevel(ledSettings.pattern,i,ledSettings.brightness,now,ledSettings.mask);}
   if(v!=previous[i]&&mp24::ledWrite(i,v))previous[i]=v;
 }
}
}
