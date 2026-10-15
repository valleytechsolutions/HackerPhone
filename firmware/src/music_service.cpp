#include "music_service.h"
#include "mp24_board.h"
#include "device_controls.h"
#include "rtttl.h"
#include "capture_format.h"
#include <driver/i2s_std.h>
#include <freertos/idf_additions.h>
#include <atomic>
#include <math.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "../../third_party/minimp3/minimp3.h"

namespace hackerphone {
static MusicStatus status;
static SemaphoreHandle_t mutex=nullptr;
static QueueHandle_t jobs=nullptr;
static std::atomic<bool> stopping{false},paused{false},ampWanted{false},repeatTrack{false};
static std::atomic<unsigned> volume{35};
static std::atomic<bool> callPause{false},cancelAlert{false};
static bool ampOn=false;
struct Job {char path[160]{};bool tone=false,rtttl=false;uint8_t level=100;RtttlTone melody;};
static i2s_chan_handle_t tx=nullptr;static uint32_t outputRate=0;
static mp3dec_t* decoder=nullptr;static uint8_t* encoded=nullptr;static int16_t* pcm=nullptr;static int32_t* output=nullptr;
static void message(const char* s){xSemaphoreTake(mutex,portMAX_DELAY);snprintf(status.message,sizeof(status.message),"%s",s);xSemaphoreGive(mutex);}
static void error(const char* s){message(s);xSemaphoreTake(mutex,portMAX_DELAY);++status.errors;xSemaphoreGive(mutex);}
static void closeOutput(){ampWanted=false;if(tx){i2s_channel_disable(tx);i2s_del_channel(tx);tx=nullptr;}outputRate=0;}
static bool openOutput(unsigned rate){
 if(tx&&outputRate==rate)return true;closeOutput();if(rate<8000||rate>48000)return false;
 i2s_chan_config_t cfg=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,I2S_ROLE_MASTER);cfg.dma_desc_num=6;cfg.dma_frame_num=256;cfg.auto_clear=true;
 if(i2s_new_channel(&cfg,&tx,nullptr)!=ESP_OK){tx=nullptr;return false;}
 i2s_std_config_t std{};std.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate);std.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO);
 std.gpio_cfg={I2S_GPIO_UNUSED,GPIO_NUM_39,GPIO_NUM_40,GPIO_NUM_38,I2S_GPIO_UNUSED,{false,false,false}};
 if(i2s_channel_init_std_mode(tx,&std)!=ESP_OK||i2s_channel_enable(tx)!=ESP_OK){closeOutput();return false;}outputRate=rate;return true;
}
static bool writePcm(unsigned samples,unsigned channels,unsigned rate,unsigned level=100){
 if(!openOutput(rate)){error("Audio I2S unavailable");return false;}
 unsigned gain=volume.load();gain=gain*gain*level/100; // perceptual master control; 100 is unity, 0 is digital mute
 for(unsigned i=0;i<samples;++i)for(unsigned c=0;c<2;++c){int32_t sample=pcm[i*channels+(channels==1?0:c)];output[i*2+c]=int32_t(int64_t(sample)*65536*gain/10000);}
 ampWanted=gain>0;size_t bytes=samples*8,sent=0;
 while(sent<bytes&&!stopping.load()){
   size_t got=0;auto e=i2s_channel_write(tx,reinterpret_cast<uint8_t*>(output)+sent,bytes-sent,&got,100);
   sent+=got;if(e!=ESP_OK){error("Audio DMA timeout");return false;}}
 return sent==bytes;
}
static void playTune(const Job& job){
 xSemaphoreTake(mutex,portMAX_DELAY);status.alert=true;snprintf(status.message,sizeof(status.message),"%s",job.melody.name);xSemaphoreGive(mutex);
 for(unsigned n=0;n<job.melody.count&&!stopping.load()&&!cancelAlert.load();++n){auto note=job.melody.notes[n];unsigned samples=uint32_t(note.milliseconds)*44100/1000,at=0;uint32_t phase=0,step=uint64_t(note.frequency)*0x100000000ULL/44100;
   while(at<samples&&!stopping.load()&&!cancelAlert.load()){unsigned block=std::min(441u,samples-at);for(unsigned i=0;i<block;++i){unsigned pos=at+i;int amp=pos<samples*85/100?6000:0;unsigned fade=std::min(200u,std::min(pos,samples-pos));amp=amp*fade/200;pcm[i]=note.frequency?(phase&0x80000000u?amp:-amp):0;phase+=step;}
     if(!writePcm(block,1,44100,job.level)){cancelAlert=true;break;}at+=block;vTaskDelay(1);}}
 xSemaphoreTake(mutex,portMAX_DELAY);status.alert=false;xSemaphoreGive(mutex);ampWanted=false;
}
static void playFile(const Job& job){
 File file=SD.open(job.path,FILE_READ);if(!file||file.isDirectory()){error("Cannot open MP3 on SD");return;}
 uint8_t header[10]{};unsigned count=file.read(header,10);uint32_t offset=0;
 if(!id3Skip(header,count,file.size(),offset)||!file.seek(offset)){file.close();error("Invalid MP3 metadata");return;}
 xSemaphoreTake(mutex,portMAX_DELAY);status.size=file.size();status.position=offset;xSemaphoreGive(mutex);
 size_t available=0;uint32_t rejected=0;uint64_t elapsedUs=0;mp3dec_init(decoder);
 while(!stopping.load()){
   Job notification;if(xQueueReceive(jobs,&notification,0)==pdTRUE&&notification.rtttl){playTune(notification);message(paused.load()?"Paused":"Playing");}
   if(paused.load()||callPause.load()){ampWanted=false;vTaskDelay(pdMS_TO_TICKS(20));continue;}
   if(available<12288&&file.available()){
     int n=file.read(encoded+available,16384-available);if(n<=0){error("SD read failed");break;}available+=n;
   }
   if(!available){
     if(repeatTrack.load()&&!stopping.load()){file.seek(offset);mp3dec_init(decoder);elapsedUs=0;continue;}
     message("Track finished");break;
   }
   mp3dec_frame_info_t info{};int samples=mp3dec_decode_frame(decoder,encoded,available,pcm,&info);
   if(info.frame_bytes<0||size_t(info.frame_bytes)>available){error("Invalid MP3 frame");break;}
   unsigned consumed=info.frame_bytes?info.frame_bytes:unsigned(available);
   if(samples>0&&info.channels>=1&&info.channels<=2&&info.hz>=8000&&info.hz<=48000){
     if(!writePcm(samples,info.channels,info.hz))break;rejected=0;elapsedUs+=uint64_t(samples)*1000000/info.hz;
     xSemaphoreTake(mutex,portMAX_DELAY);++status.frames;status.rate=info.hz;status.channels=info.channels;status.elapsedMs=elapsedUs/1000;status.position=file.position()-available+consumed;status.stackFree=uxTaskGetStackHighWaterMark(nullptr);xSemaphoreGive(mutex);
   }else if((rejected+=consumed)>65536){error("No valid MP3 audio frames");break;}
   memmove(encoded,encoded+consumed,available-consumed);available-=consumed;vTaskDelay(1);
 }file.close();
}
static void worker(void*){
 Job job;for(;;){if(xQueueReceive(jobs,&job,portMAX_DELAY)!=pdTRUE)continue;
   xSemaphoreTake(mutex,portMAX_DELAY);status.busy=status.playing=true;xSemaphoreGive(mutex);
   if(job.rtttl){playTune(job);message("Tone finished");}
   else if(job.tone){for(unsigned block=0;block<60&&!stopping.load();++block){for(unsigned i=0;i<441;++i){unsigned sample=block*441+i;float fade=fminf(1.f,fminf(sample/441.f,(26460-sample)/441.f));pcm[i]=int16_t(sinf(sample*6.2831853f*660/44100)*8000*fade);}if(!writePcm(441,1,44100))break;vTaskDelay(1);}message("Audio test completed");}
   else playFile(job);
   // Let the last DMA blocks leave the output before disabling it.
   if(!stopping.load())vTaskDelay(pdMS_TO_TICKS(45));closeOutput();
   xSemaphoreTake(mutex,portMAX_DELAY);status.busy=status.playing=uxQueueMessagesWaiting(jobs)>0;status.paused=false;if(stopping.load())snprintf(status.message,sizeof(status.message),"Stopped");xSemaphoreGive(mutex);
 }
}
static bool prepare(){
 if(jobs)return true;
 decoder=static_cast<mp3dec_t*>(ps_calloc(1,sizeof(mp3dec_t)));encoded=static_cast<uint8_t*>(ps_malloc(16384));pcm=static_cast<int16_t*>(ps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME*2));output=static_cast<int32_t*>(ps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME*4));
 if(!decoder||!encoded||!pcm||!output)goto fail;
 jobs=xQueueCreate(1,sizeof(Job));if(!jobs)goto fail;
 // MP3 scratch and stack live in PSRAM, leaving internal RAM for radio and DMA.
 if(xTaskCreatePinnedToCoreWithCaps(worker,"hp-music",32768,nullptr,1,nullptr,1,MALLOC_CAP_SPIRAM)!=pdPASS){vQueueDelete(jobs);jobs=nullptr;goto fail;}
 status.ready=true;return true;
 fail:free(decoder);free(encoded);free(pcm);free(output);decoder=nullptr;encoded=nullptr;pcm=nullptr;output=nullptr;return false;
}
void musicBegin(uint8_t v,bool r){if(!mutex)mutex=xSemaphoreCreateMutex();volume=std::min<unsigned>(100,v);repeatTrack=r;}
MusicStatus musicStatus(){if(mutex)xSemaphoreTake(mutex,portMAX_DELAY);auto s=status;if(mutex)xSemaphoreGive(mutex);s.volume=volume.load();s.paused=paused.load()&&s.busy;s.repeat=repeatTrack.load();return s;}
static bool start(const Job& job){if(!mutex||musicStatus().busy||!prepare())return false;stopping=paused=cancelAlert=false;
 xSemaphoreTake(mutex,portMAX_DELAY);status.busy=status.playing=true;status.errors=status.frames=status.elapsedMs=status.position=status.rate=0;snprintf(status.path,sizeof(status.path),"%s",job.path);snprintf(status.message,sizeof(status.message),job.tone?"Audio test":"Playing");xSemaphoreGive(mutex);
 if(xQueueSend(jobs,&job,0)==pdTRUE)return true;xSemaphoreTake(mutex,portMAX_DELAY);status.busy=status.playing=false;xSemaphoreGive(mutex);return false;}
bool musicPlay(const char* path){if(!mp24::cardReady||!safeSdPath(path))return false;String lower(path);lower.toLowerCase();if(!lower.endsWith(".mp3"))return false;Job j;snprintf(j.path,sizeof(j.path),"%s",path);return start(j);}
bool musicTone(){Job j;j.tone=true;return start(j);}
bool musicRtttl(const char* text,uint8_t level){Job j;j.rtttl=true;j.level=std::min<uint8_t>(100,level);if(!parseRtttl(text,j.melody))return false;if(!musicStatus().busy)return start(j);if(!jobs||stopping.load())return false;cancelAlert=false;return xQueueSend(jobs,&j,0)==pdTRUE;}
void musicCallPause(bool pause){callPause=pause;}
void musicCancelAlert(){cancelAlert=true;}
void musicStop(){stopping=true;paused=false;ampWanted=false;if(jobs)xQueueReset(jobs);}
void musicPause(){if(musicStatus().busy)paused=!paused.load();}
void musicVolume(int v){volume=std::max(0,std::min(100,v));}
void musicRepeat(bool r){repeatTrack=r;}
void musicTick(){bool wanted=ampWanted.load()&&!stopping.load();if(wanted!=ampOn&&mp24::audioEnable(wanted))ampOn=wanted;}
bool musicNext(int direction){
 auto s=musicStatus();if(s.busy||!*s.path)return false;String folder=s.path;int slash=folder.lastIndexOf('/');folder=slash<=0?"/":folder.substring(0,slash);
 File dir=SD.open(folder);if(!dir||!dir.isDirectory())return false;String first,last,previous,chosen;bool found=false;unsigned count=0;
 while(count++<1024){File f=dir.openNextFile();if(!f)break;String name=f.name();bool isDir=f.isDirectory();f.close();int idx=name.lastIndexOf('/');if(idx>=0)name=name.substring(idx+1);String lower=name;lower.toLowerCase();if(isDir||!lower.endsWith(".mp3"))continue;String path=folder+(folder=="/"?"":"/")+name;if(first.isEmpty())first=path;last=path;
   if(found&&direction>0){chosen=path;break;}if(path==s.path){found=true;if(direction<0&&!previous.isEmpty()){chosen=previous;break;}}previous=path;
 }dir.close();if(chosen.isEmpty())chosen=direction>0?first:last;return !chosen.isEmpty()&&musicPlay(chosen.c_str());
}
}
