#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include "phone_core.h"

namespace mp24 {
constexpr int Width=160,Height=128;
enum Key : uint8_t { K0,K1,K2,K3,K4,K5,K6,K7,K8,K9,Star,Hash,A,B,C,D,Up,Down,Left,Right,Enter,VolumeUp,VolumeDown,None=255 };
extern TFT_eSPI lcd;
extern TFT_eSprite frame;
extern bool cardReady,keysReady,lightReady;
extern uint32_t keyEvents,i2cErrors;
bool begin();
Key key(uint32_t now);
void present();
void backlight(bool on);
bool ledsBegin();
bool ledWrite(unsigned index,uint8_t brightness);
bool audioEnable(bool on);
bool powerPressed();
bool powerCut();
void modemShutdown();
struct ModemTransport:hackerphone::AtTransport{bool write(const char* p,size_t n)override;};
void modemBegin();
void modemReleaseUart();
void modemResumeUart();
void modemPoll(hackerphone::PhoneCore& phone,uint32_t now);
}
