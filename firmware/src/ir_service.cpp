// Original MIT IR service. The sample's factory initialization identifies
// RX GPIO42 (pull-up) and inverted TX GPIO45. No vendor application is linked.
#include "ir_service.h"
#include "mp24_board.h"
#include "capture_format.h"
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <driver/gpio.h>
#include <atomic>
#include <algorithm>
#include <new>
#include <esp_random.h>

namespace hackerphone {
IrStatus irStatus;
void irBegin(){gpio_set_level(GPIO_NUM_45,1);gpio_set_direction(GPIO_NUM_45,GPIO_MODE_OUTPUT);}
static IrSignal* signal=nullptr;
static IrSignal* nextSignal=nullptr;
static uint32_t learnFrequency=38000,started=0;
static rmt_channel_handle_t rx=nullptr,tx=nullptr;
static rmt_encoder_handle_t encoder=nullptr;
static bool rxEnabled=false,txEnabled=false;
static constexpr size_t RxCapacity=640,TxCapacity=1100;
static rmt_symbol_word_t* rxBuffer=nullptr;
static rmt_symbol_word_t* txBuffer=nullptr;
static std::atomic<size_t> received{0};
static std::atomic<bool> rxDone{false},txDone{false};
static File library;
static IrReader reader;
static uint32_t offsets[32]{};
static unsigned historyBase=0,historyCount=0;

static void message(const char* text){if(text!=irStatus.message)snprintf(irStatus.message,sizeof(irStatus.message),"%s",text);}
static bool failure(const char* text){irStatus.state=IrState::Error;message(text);return false;}
bool irBusy(){return irStatus.state==IrState::Learning||irStatus.state==IrState::Sending;}
const IrSignal* irSignal(){return signal;}
uint32_t irFrequency(){return learnFrequency;}
static bool allocateSignal(){if(!signal){void* p=ps_malloc(sizeof(IrSignal));if(p)signal=new(p) IrSignal;}return signal||failure("Not enough memory for IR");}
static void closeLibrary(){library.close();reader.reset();irStatus.library=false;irStatus.record=0;historyBase=historyCount=0;}
static bool IRAM_ATTR onReceive(rmt_channel_handle_t,const rmt_rx_done_event_data_t* event,void*){
  received.store(event->num_symbols,std::memory_order_relaxed);rxDone.store(true,std::memory_order_release);return false;
}
static bool IRAM_ATTR onTransmit(rmt_channel_handle_t,const rmt_tx_done_event_data_t*,void*){txDone.store(true,std::memory_order_release);return false;}
// Channels and DMA memory exist only during an active learn/send operation.
// Disabling the channel cancels hardware before its buffer is released.
static void releaseRadio(){
  if(rx){if(rxEnabled)rmt_disable(rx);rxEnabled=false;rmt_del_channel(rx);rx=nullptr;}
  if(tx){if(txEnabled)rmt_disable(tx);txEnabled=false;rmt_del_channel(tx);tx=nullptr;gpio_reset_pin(GPIO_NUM_45);gpio_set_level(GPIO_NUM_45,1);gpio_set_direction(GPIO_NUM_45,GPIO_MODE_OUTPUT);}
  if(encoder){rmt_del_encoder(encoder);encoder=nullptr;}
  free(rxBuffer);rxBuffer=nullptr;free(txBuffer);txBuffer=nullptr;
  rxDone.store(false);txDone.store(false);
}
void irStop(){bool active=irBusy();releaseRadio();if(active){irStatus.state=signal&&validIrSignal(*signal)?IrState::Ready:IrState::Idle;message("Stopped");}}
bool irSetFrequency(uint32_t hz){if(irBusy()||hz<10000||hz>100000)return false;learnFrequency=hz;if(signal&&signal->count){signal->frequency=hz;irStatus.unsaved=true;}return true;}
bool irDirectories(){
  if(!mp24::cardReady)return failure("Insert an SD card to save");
  for(const char* path:{"/hackerphone","/hackerphone/ir","/hackerphone/ir/recordings","/hackerphone/ir/database"})
    if(!SD.exists(path)&&!SD.mkdir(path))return failure("Cannot create IR folder");
  return true;
}
static bool receiveAgain(){
  received.store(0);rxDone.store(false);
  rmt_receive_config_t config{};config.signal_range_min_ns=1000;config.signal_range_max_ns=30000000;
  if(rmt_receive(rx,rxBuffer,RxCapacity*sizeof(*rxBuffer),&config)!=ESP_OK){releaseRadio();return failure("IR receiver start failed");}return true;
}
bool irLearn(){
  if(irBusy())return false;if(!allocateSignal())return false;
  closeLibrary();signal->count=0;irStatus.unsaved=false;irStatus.path[0]=0;
  rxBuffer=static_cast<rmt_symbol_word_t*>(heap_caps_calloc(RxCapacity,sizeof(*rxBuffer),MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
  rmt_rx_channel_config_t cfg{};cfg.gpio_num=GPIO_NUM_42;cfg.clk_src=RMT_CLK_SRC_DEFAULT;cfg.resolution_hz=1000000;cfg.mem_block_symbols=RxCapacity;cfg.flags.with_dma=true;cfg.flags.invert_in=true;
  if(!rxBuffer||rmt_new_rx_channel(&cfg,&rx)!=ESP_OK){releaseRadio();return failure("IR receive allocation failed");}
  gpio_set_pull_mode(GPIO_NUM_42,GPIO_PULLUP_ONLY);
  // The module has a raw photodiode. Demodulate its active-low carrier after
  // input inversion. IDF recommends frequency tolerance on the receive side.
  rmt_carrier_config_t carrier{};carrier.frequency_hz=learnFrequency*2/3;carrier.duty_cycle=.33f;
  rmt_rx_event_callbacks_t callbacks{};callbacks.on_recv_done=onReceive;
  if(rmt_apply_carrier(rx,&carrier)!=ESP_OK||rmt_rx_register_event_callbacks(rx,&callbacks,nullptr)!=ESP_OK||rmt_enable(rx)!=ESP_OK){releaseRadio();return failure("IR receiver setup failed");}
  rxEnabled=true;if(!receiveAgain())return false;
  started=millis();irStatus.state=IrState::Learning;message("Press remote within 10 sec");return true;
}
static bool decodeReceived(size_t n){
  if(n>=RxCapacity)return false;signal->count=0;bool last=false;
  for(size_t i=0;i<n*2;++i){auto& symbol=rxBuffer[i/2];uint32_t duration=i%2?symbol.duration1:symbol.duration0;bool mark=i%2?symbol.level1:symbol.level0;
    if(!duration)break;if(!signal->count&&!mark)continue;
    if(!mark&&duration>=29000)break;
    if(signal->count&&mark==last){signal->durations[signal->count-1]+=duration;continue;}
    if(signal->count>=IrSignal::Capacity)return false;
    signal->durations[signal->count++]=duration;last=mark;
  }
  signal->frequency=learnFrequency;signal->duty=.33f;
  // Tiny pulses are carrier leakage/noise, not a learned remote envelope.
  if(signal->count<6)return false;for(unsigned i=0;i<signal->count;++i)if(signal->durations[i]<80)return false;
  return validIrSignal(*signal);
}
void irTick(uint32_t now){
  if(irStatus.state==IrState::Learning){
    if(rxDone.exchange(false,std::memory_order_acquire)){
      size_t n=received.load(std::memory_order_relaxed);
      if(n>=RxCapacity){signal->count=0;releaseRadio();failure("Signal too long; not recorded");return;}
      if(decodeReceived(n)){
        releaseRadio();snprintf(signal->name,sizeof(signal->name),"Recorded-%lu",(unsigned long)++irStatus.learned);irStatus.unsaved=true;irStatus.state=IrState::Ready;message("Captured. D saves to SD");return;
      }
      signal->count=0;if(!receiveAgain())return;
    }
    if(uint32_t(now-started)>=10000){releaseRadio();signal->count=0;irStatus.state=IrState::Idle;message("No usable IR signal received");}
  }else if(irStatus.state==IrState::Sending){
    if(txDone.exchange(false,std::memory_order_acquire)){releaseRadio();++irStatus.sent;irStatus.state=IrState::Ready;message("IR transmission completed");}
    else if(uint32_t(now-started)>3000){releaseRadio();failure("IR transmit timeout");}
  }
}
bool irSend(){
  if(irBusy())return false;if(!signal||!validIrSignal(*signal))return failure("Load or record a signal first");
  txBuffer=static_cast<rmt_symbol_word_t*>(heap_caps_calloc(TxCapacity,sizeof(*txBuffer),MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
  if(!txBuffer)return failure("IR transmit memory unavailable");
  size_t halves=0;
  for(unsigned i=0;i<signal->count;++i){uint32_t left=signal->durations[i];while(left){if(halves>=TxCapacity*2){releaseRadio();return failure("Signal exceeds transmit limit");}uint16_t part=std::min<uint32_t>(left,32767);auto& word=txBuffer[halves/2];bool mark=i%2==0;if(halves%2){word.duration1=part;word.level1=mark;}else{word.duration0=part;word.level0=mark;}++halves;left-=part;}}
  if(halves%2){txBuffer[halves/2].duration1=1;txBuffer[halves/2].level1=0;++halves;}
  // Set the inverted emitter idle before connecting the peripheral output.
  gpio_set_level(GPIO_NUM_45,1);gpio_set_direction(GPIO_NUM_45,GPIO_MODE_OUTPUT);
  rmt_tx_channel_config_t cfg{};cfg.gpio_num=GPIO_NUM_45;cfg.clk_src=RMT_CLK_SRC_DEFAULT;cfg.resolution_hz=1000000;cfg.mem_block_symbols=256;cfg.trans_queue_depth=1;cfg.flags.invert_out=true;cfg.flags.with_dma=true;
  rmt_copy_encoder_config_t enc{};
  if(rmt_new_tx_channel(&cfg,&tx)!=ESP_OK||rmt_new_copy_encoder(&enc,&encoder)!=ESP_OK){releaseRadio();return failure("IR transmitter allocation failed");}
  rmt_carrier_config_t carrier{};carrier.frequency_hz=signal->frequency;carrier.duty_cycle=signal->duty;
  rmt_tx_event_callbacks_t callbacks{};callbacks.on_trans_done=onTransmit;
  if(rmt_apply_carrier(tx,&carrier)!=ESP_OK||rmt_tx_register_event_callbacks(tx,&callbacks,nullptr)!=ESP_OK||rmt_enable(tx)!=ESP_OK){releaseRadio();return failure("IR transmitter setup failed");}
  txEnabled=true;txDone.store(false);rmt_transmit_config_t config{};config.flags.queue_nonblocking=true;config.flags.eot_level=0;
  if(rmt_transmit(tx,encoder,txBuffer,halves/2*sizeof(*txBuffer),&config)!=ESP_OK){releaseRadio();return failure("IR transmit request failed");}
  irStatus.state=IrState::Sending;started=millis();message("Transmitting once; B stops");return true;
}
// Buffered reads avoid a filesystem call for each character, while preserving
// the logical position when the parser stops before the end of a disk block.
class CardInput:public IrInput {
 public:explicit CardInput(File& file):file_(file){}~CardInput(){if(size_>at_)file_.seek(file_.position()-(size_-at_));}
  int read()override{if(at_==size_){int n=file_.read(buffer_,sizeof(buffer_));if(n<=0)return -1;size_=n;at_=0;}return buffer_[at_++];}
 private:File& file_;uint8_t buffer_[512];size_t at_=0,size_=0;
};
static bool readRecord(){
  if(!nextSignal){void* p=ps_malloc(sizeof(IrSignal));if(p)nextSignal=new(p) IrSignal;}
  if(!nextSignal)return failure("IR parser memory unavailable");
  uint32_t offset=library.position();IrReadResult result;{CardInput input(library);result=reader.next(input,*nextSignal,irStatus.message,sizeof(irStatus.message));}
  if(result==IrReadResult::End){message("End of library; Left goes back");return false;}
  if(result==IrReadResult::Invalid){signal->count=0;closeLibrary();return failure(irStatus.message);}
  *signal=*nextSignal;
  if(historyCount==32){for(unsigned i=1;i<32;++i)offsets[i-1]=offsets[i];--historyCount;++historyBase;}
  offsets[historyCount++]=offset;++irStatus.record;
  irStatus.unsaved=false;irStatus.state=result==IrReadResult::Ready?IrState::Ready:IrState::Idle;
  if(result==IrReadResult::Ready)message("A sends; Left/Right buttons");return result==IrReadResult::Ready;
}
bool irOpen(const char* path){
  if(irBusy())return false;
  if(!mp24::cardReady||!validIrLibraryPath(path))return failure("Invalid IR library path");
  if(!allocateSignal())return false;closeLibrary();signal->count=0;irStatus.unsaved=false;
  library=SD.open(path,FILE_READ);if(!library||library.isDirectory()){closeLibrary();return failure("Cannot open IR file");}
  irStatus.library=true;snprintf(irStatus.path,sizeof(irStatus.path),"%s",path);return readRecord();
}
bool irNext(){if(irBusy()||!library)return false;return readRecord();}
bool irPrevious(){
  if(irBusy()||!library||historyCount<2){message("No earlier button in window");return false;}
  uint32_t offset=offsets[historyCount-2];historyCount-=2;irStatus.record-=2;
  if(!library.seek(offset))return failure("IR library seek failed");return readRecord();
}
bool irSave(const char* name){
  if(irBusy()||!signal||!validIrSignal(*signal))return false;
  if(name){size_t n=strlen(name);if(!n||n>=sizeof(signal->name))return false;for(size_t i=0;i<n;++i)if(name[i]<32||name[i]>126||name[i]==':')return false;snprintf(signal->name,sizeof(signal->name),"%s",name);}
  if(!irDirectories())return false;
  char path[96],pending[104];bool found=false;
  for(unsigned attempt=0;attempt<16;++attempt){snprintf(path,sizeof(path),"/hackerphone/ir/recordings/remote-%08lx-%04x.ir",(unsigned long)millis(),unsigned(esp_random()&0xffff));snprintf(pending,sizeof(pending),"%s.tmp",path);if(!SD.exists(path)&&!SD.exists(pending)){found=true;break;}}
  if(!found)return failure("Recording names exhausted");
  File out=SD.open(pending,FILE_WRITE);if(!out)return failure("Cannot create IR recording");
  char header[256];int n=snprintf(header,sizeof(header),"Filetype: IR signals file\nVersion: 1\n# HackerPhone raw recording\nname: %s\ntype: raw\nfrequency: %lu\nduty_cycle: %.4f\ndata:",signal->name,(unsigned long)signal->frequency,signal->duty);
  bool ok=n>0&&size_t(n)<sizeof(header)&&out.write((const uint8_t*)header,n)==size_t(n);
  char block[256];size_t used=0;
  for(unsigned i=0;ok&&i<signal->count;++i){char value[16];n=snprintf(value,sizeof(value)," %lu",(unsigned long)signal->durations[i]);
    if(used+size_t(n)>=sizeof(block)){ok=out.write((const uint8_t*)block,used)==used;used=0;}
    memcpy(block+used,value,n);used+=n;
  }
  if(ok){block[used++]='\n';ok=out.write((const uint8_t*)block,used)==used;}out.flush();out.close();
  if(!ok||!SD.rename(pending,path))return failure("SD save failed; partial .tmp kept");
  irStatus.unsaved=false;irStatus.state=IrState::Ready;if(!irStatus.library)snprintf(irStatus.path,sizeof(irStatus.path),"%s",path);message("Saved new recording on SD");return true;
}
bool irStorageTest(){
  if(irBusy()||irStatus.unsaved||!irDirectories())return false;
  char path[96];snprintf(path,sizeof(path),"/hackerphone/ir/selftest-%08lx.ir",(unsigned long)millis());if(SD.exists(path))return false;
  const char fixture[]="Filetype: IR signals file\nVersion: 1\nname: Test A\ntype: raw\nfrequency: 38000\nduty_cycle: .33\ndata: 9000 4500 560 560 560 1690 560\n# next\nname: Test B\ntype: parsed\nprotocol: NEC\naddress: 10 00 00 00\ncommand: 34 00 00 00\n";
  File out=SD.open(path,FILE_WRITE);if(!out)return false;bool ok=out.write((const uint8_t*)fixture,sizeof(fixture)-1)==sizeof(fixture)-1;out.flush();out.close();
  ok=ok&&irOpen(path)&&signal->count==7&&!strcmp(signal->name,"Test A")&&irNext()&&signal->count==67&&!strcmp(signal->name,"Test B");
  ok=ok&&!irNext()&&signal->count==67&&irPrevious()&&!strcmp(signal->name,"Test A")&&irStatus.record==1;
  closeLibrary();if(signal)signal->count=0;irStatus.path[0]=0;irStatus.unsaved=false;irStatus.state=IrState::Idle;
  ok=SD.remove(path)&&ok;message(ok?"IR SD test passed (no transmit)":"IR SD test failed");return ok;
}
}
