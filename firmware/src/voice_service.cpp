#include "voice_service.h"
#include "music_service.h"
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <driver/i2s_tdm.h>
#include <freertos/idf_additions.h>
#include <atomic>
#include <algorithm>

namespace hackerphone {
namespace {
std::atomic<bool> running{false},stopping{false},active{false};
std::atomic<uint32_t> frames{0},errors{0};
std::atomic<uint16_t> downPeak{0},micLeft{0},micRight{0};
std::atomic<uint16_t> otherPeak{0};
std::atomic<int> lastError{0};
std::atomic<unsigned> stage{0};
std::atomic<uint32_t> testUntil{0};
bool attempted=false;
bool checked(esp_err_t result,unsigned step){if(result==ESP_OK)return true;lastError=result;stage=step;return false;}
void close(i2s_chan_handle_t& channel){if(channel){i2s_chan_info_t info{};if(i2s_channel_get_info(channel,&info)==ESP_OK&&info.is_enabled)i2s_channel_disable(channel);i2s_del_channel(channel);channel=nullptr;}}
bool duplex(i2s_port_t port,i2s_std_config_t& config,i2s_chan_handle_t& tx,i2s_chan_handle_t& rx){
 i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(port,I2S_ROLE_MASTER);channel.dma_desc_num=4;channel.dma_frame_num=120;channel.auto_clear=true;
 if(!checked(i2s_new_channel(&channel,&tx,&rx),6))return false;
 return checked(i2s_channel_init_std_mode(tx,&config),7)&&checked(i2s_channel_init_std_mode(rx,&config),8)&&checked(i2s_channel_enable(rx),9)&&checked(i2s_channel_enable(tx),10);
}
void worker(void*){
 i2s_chan_handle_t pcmTx=nullptr,pcmRx=nullptr,audioTx=nullptr,audioRx=nullptr;
 // Pins and 8 kHz short-frame PCM recovered from this sample's hardware
 // configuration and diagnostic arguments. Use eight 16-bit slots at 16 kHz
 // for 2.048 MHz BCLK; only the first slot carries voice.
 constexpr unsigned slots=8, ratio=3;
 i2s_tdm_config_t pcm{};pcm.clk_cfg=I2S_TDM_CLK_DEFAULT_CONFIG(16000);pcm.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_512;
 pcm.slot_cfg=I2S_TDM_PCM_SHORT_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO,static_cast<i2s_tdm_slot_mask_t>(0xff));pcm.slot_cfg.total_slot=slots;
 // EG91 samples on the falling edge and changes data on the rising edge.
 pcm.gpio_cfg={I2S_GPIO_UNUSED,GPIO_NUM_14,GPIO_NUM_21,GPIO_NUM_33,GPIO_NUM_13,{false,true,false}};
 i2s_std_config_t audio{};audio.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(48000);audio.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO);
 audio.gpio_cfg={I2S_GPIO_UNUSED,GPIO_NUM_39,GPIO_NUM_40,GPIO_NUM_38,GPIO_NUM_41,{false,false,false}};
 i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);channel.dma_desc_num=4;channel.dma_frame_num=80;channel.auto_clear=true;
 bool pcmReady=checked(i2s_new_channel(&channel,&pcmTx,&pcmRx),1)&&checked(i2s_channel_init_tdm_mode(pcmTx,&pcm),2)&&checked(i2s_channel_init_tdm_mode(pcmRx,&pcm),3)&&checked(i2s_channel_enable(pcmRx),4)&&checked(i2s_channel_enable(pcmTx),5);
 if(pcmReady&&duplex(I2S_NUM_1,audio,audioTx,audioRx)){
  active=true;musicVoice(true);
  int16_t down[80*slots]{},up[80*slots]{};int32_t speaker[80*ratio*2]{},mic[80*ratio*2]{};
  while(!stopping.load()){
   size_t got=0;if(i2s_channel_read(pcmRx,down,sizeof(down),&got,50)!=ESP_OK||!got){++errors;continue;}
   unsigned count=std::min<unsigned>(80,got/(slots*2)),volume=musicStatus().volume;unsigned gain=volume*volume;uint16_t peak=0,other=0;
   for(unsigned i=0;i<count;++i){int16_t sample=down[i*slots];peak=std::max<uint16_t>(peak,abs(int(sample)));for(unsigned slot=1;slot<slots;++slot)other=std::max<uint16_t>(other,abs(int(down[i*slots+slot])));int32_t value=int64_t(sample)*65536*gain/10000;for(unsigned j=0;j<ratio;++j)speaker[(i*ratio+j)*2]=speaker[(i*ratio+j)*2+1]=value;}otherPeak=other;
   size_t sent=0;if(i2s_channel_write(audioTx,speaker,count*ratio*8,&sent,50)!=ESP_OK||sent!=count*ratio*8)++errors;downPeak=peak;
   got=0;if(i2s_channel_read(audioRx,mic,count*ratio*8,&got,50)!=ESP_OK||got!=count*ratio*8){++errors;memset(up,0,sizeof(up));}
   else {uint16_t left=0,right=0;for(unsigned i=0;i<count*ratio;++i){left=std::max<uint16_t>(left,abs(int(mic[i*2]>>16)));right=std::max<uint16_t>(right,abs(int(mic[i*2+1]>>16)));}micLeft=left;micRight=right;unsigned slot=right>left?1:0;
    for(unsigned i=0;i<count;++i){int32_t sum=0;for(unsigned j=0;j<ratio;++j)sum+=mic[(i*ratio+j)*2+slot]>>16;up[i*slots]=int16_t(sum/int32_t(ratio));}}
   sent=0;if(i2s_channel_write(pcmTx,up,count*slots*2,&sent,50)!=ESP_OK||sent!=count*slots*2)++errors;++frames;
  }
 }else ++errors;
 active=false;close(audioRx);close(audioTx);close(pcmRx);close(pcmTx);musicVoice(false);running=false;vTaskDeleteWithCaps(nullptr);
}
}
VoiceStatus voiceStatus(){return {active.load(),running.load(),frames.load(),errors.load(),downPeak.load(),micLeft.load(),micRight.load(),otherPeak.load(),lastError.load(),stage.load()};}
void voiceStop(){testUntil=0;stopping=true;}
bool voiceTest(const PhoneState& phone,uint32_t now){if(phone.phonePriority()||running.load()||musicStatus().busy)return false;testUntil=now+3000;attempted=false;return true;}
void voiceTick(const PhoneState& phone){
 uint32_t until=testUntil.load();bool test=until&&int32_t(until-millis())>0;
 bool wanted=test||phone.call==Call::Dialing||phone.call==Call::Alerting||phone.call==Call::Active||phone.call==Call::Held;
 if(!wanted){voiceStop();if(!running.load())attempted=false;return;}
 if(running.load()||attempted)return;
 if(musicStatus().busy){musicStop();return;}
 attempted=true;stopping=false;running=true;musicVoice(true);frames=errors=0;downPeak=micLeft=micRight=otherPeak=0;lastError=0;stage=0;
 if(xTaskCreatePinnedToCoreWithCaps(worker,"hp-voice",24576,nullptr,4,nullptr,1,MALLOC_CAP_SPIRAM)!=pdPASS){musicVoice(false);running=false;lastError=ESP_ERR_NO_MEM;stage=11;++errors;}
}
}
