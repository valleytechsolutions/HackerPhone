#include "mp24_board.h"
#include "mp24_display.h"
#include <Wire.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>

namespace mp24 {
TFT_eSPI lcd;
TFT_eSprite frame(&lcd);
SPIClass cardBus(HSPI);
bool cardReady=false,keysReady=false,lightReady=false;
uint32_t keyEvents=0,i2cErrors=0;
static HardwareSerial modem(1);
static uint16_t lastBanks[2]={0xffff,0xffff};
static uint32_t rawKeys=0,stableKeys=0,changedAt=0,lastPoll=0,pendingKeys=0;
static bool rd(uint8_t addr,uint8_t reg,uint8_t* out,size_t n){
  Wire.beginTransmission(addr);Wire.write(reg);
  if(Wire.endTransmission(false)!=0||Wire.requestFrom(addr,uint8_t(n))!=n){++i2cErrors;return false;}
  for(size_t i=0;i<n;++i)out[i]=Wire.read();return true;
}
static bool wr(uint8_t addr,uint8_t reg,uint8_t value){Wire.beginTransmission(addr);Wire.write(reg);Wire.write(value);if(Wire.endTransmission()){++i2cErrors;return false;}return true;}
static bool mask(uint8_t addr,uint8_t reg,uint8_t set,uint8_t clear){uint8_t v;if(!rd(addr,reg,&v,1))return false;return wr(addr,reg,uint8_t((v|set)&~clear));}
void backlight(bool on){lightReady=mask(0x5b,3,on?0:1,on?1:0);}
bool begin(){
  Serial.println("HP_STAGE i2c");
  Wire.begin(47,48,100000);Wire.setTimeOut(20);
  uint8_t id=0;
  if(rd(0x5b,0x10,&id,1)&&id==0x23){
    // AW9523 P1_0 is an active-low backlight output on this sample.
    mask(0x5b,0x13,1,0); // GPIO mode, preserve the other LED channels.
    mask(0x5b,0x05,0,1);backlight(true);
  }
  // XL9555 input banks. Preserve output latches and unrelated directions.
  keysReady=mask(0x20,6,0xff,0)&&mask(0x20,7,0x77,0)&&mask(0x21,6,0x7f,0)&&mask(0x21,7,0x18,0);
  // Keep the speaker amplifier muted until the audio pipeline is verified.
  mask(0x21,3,0,2);mask(0x21,7,0,2);
  Serial.println("HP_STAGE display");
  if(!displayBegin()){Serial.println("HP_FAIL panel SPI");return false;}
  Serial.println("HP_STAGE framebuffer");
  frame.setColorDepth(16);frame.setTextFont(1);frame.setTextWrap(false);
  if(!frame.createSprite(Width,Height))return false;
  Serial.println("HP_STAGE sd");
  cardBus.begin(36,37,35,34);pinMode(34,OUTPUT);digitalWrite(34,HIGH);
  cardReady=SD.begin(34,cardBus,4000000,"/sdcard",5,false);
  return true;
}
Key key(uint32_t now){
  static constexpr uint8_t bank[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1};
  static constexpr uint8_t bit[]={6,12,0,1,10,7,2,9,5,3,8,4,14,13,5,6,0,11,12,2,1,3,4};
  if(uint32_t(now-lastPoll)>=10){lastPoll=now;uint8_t bytes[4];
    if(rd(0x20,0,bytes,2)&&rd(0x21,0,bytes+2,2)){
      lastBanks[0]=uint16_t(bytes[0])|(uint16_t(bytes[1])<<8);lastBanks[1]=uint16_t(bytes[2])|(uint16_t(bytes[3])<<8);
      uint32_t current=0;for(unsigned i=0;i<23;++i)if(!(lastBanks[bank[i]]&(1u<<bit[i])))current|=1u<<i;
      if(current!=rawKeys){rawKeys=current;changedAt=now;}
      if(uint32_t(now-changedAt)>=25&&rawKeys!=stableKeys){pendingKeys|=rawKeys&~stableKeys;stableKeys=rawKeys;}
    }
  }
  for(unsigned i=0;i<23;++i)if(pendingKeys&(1u<<i)){pendingKeys&=~(1u<<i);++keyEvents;return Key(i);}
  return None;
}
void present(){displayPresent(frame.getPointer());}
bool ModemTransport::write(const char* p,size_t n){return modem.write(reinterpret_cast<const uint8_t*>(p),n)==n;}
void modemBegin(){
  // Pins recovered from the sample's UART1 configuration, not original Ringo.
  gpio_set_level(GPIO_NUM_12,0);pinMode(12,OUTPUT);gpio_set_level(GPIO_NUM_16,0);pinMode(16,OUTPUT);
  gpio_set_level(GPIO_NUM_11,0);pinMode(11,OUTPUT);
  modem.setRxBufferSize(2048);modem.begin(115200,SERIAL_8N1,18,17);
}
void modemPoll(hackerphone::PhoneCore& phone,uint32_t now){
  static bool begun=false,pulsed=false,pulsing=false,probeSent=false;
  static uint32_t bootAt=now,pulseAt=0;static char probe[160]{};static size_t used=0;
  uint8_t data[192];size_t n=0;
  while(modem.available()&&n<sizeof(data))data[n++]=uint8_t(modem.read());
  if(!begun){
    if(!probeSent){modem.print("AT\r");probeSent=true;}
    for(size_t i=0;i<n;++i){char c=char(data[i]);if(c=='\n'||used+1>=sizeof(probe)){probe[used]=0;if(strstr(probe,"OK")||strstr(probe,"RDY")){begun=true;phone.begin(now);}used=0;}else if(c!='\r')probe[used++]=c;}
    if(!begun&&!pulsed&&uint32_t(now-bootAt)>3500){pulsed=pulsing=true;pulseAt=now;digitalWrite(12,HIGH);}
    if(pulsing&&uint32_t(now-pulseAt)>=2500){digitalWrite(12,LOW);pulsing=false;modem.print("AT\r");}
    if(!begun&&!pulsing&&uint32_t(now-bootAt)>10000&&uint32_t(now-pulseAt)>3000){modem.print("AT\r");pulseAt=now;}
  }else {phone.feed(data,n,now);phone.tick(now);}
}
}
