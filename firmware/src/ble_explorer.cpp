#include "ble_explorer.h"
#include "radio_service.h"
#include "network_service.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
namespace hackerphone {
static SemaphoreHandle_t mutex=nullptr;
static QueueHandle_t jobs=nullptr;
static NimBLEClient* client=nullptr; // Persistent; never deleted under a callback.
static GattStatus status;
static GattRow rows[48];
static std::atomic<bool> cancel{false};
struct Job{uint8_t op=0,mac[6]{},type=0;uint16_t handle=0;uint8_t bytes[64]{},size=0;bool response=true;};
static void message(const char* text){xSemaphoreTake(mutex,portMAX_DELAY);snprintf(status.message,sizeof(status.message),"%s",text);xSemaphoreGive(mutex);}
static void value(uint16_t handle,const uint8_t* bytes,size_t n,bool notification){
  if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(5))!=pdTRUE)return;
  status.valueHandle=handle;status.valueSize=std::min(n,sizeof(status.value));memcpy(status.value,bytes,status.valueSize);
  if(notification)++status.notifications;xSemaphoreGive(mutex);
}
static void trackerSound(){
  struct Protocol{const char* service;const char* characteristic;uint8_t bytes[3],length;};
  static const Protocol protocols[]={
    {"7dfc9000-7d1c-4951-86aa-8d9728f8d66c","7dfc9001-7d1c-4951-86aa-8d9728f8d66c",{0xaf,0,0},1},
    {"0000fd44-0000-1000-8000-00805f9b34fb","4f860003-943b-49ef-bed4-2f730304427a",{1,0,3},3},
    {"15190001-12f4-c226-88ed-2ac5579f2a85","8e0c0001-1d68-fb92-bf61-48377421680e",{0,3,0},2}
  };
  bool found=false;
  for(const auto& p:protocols){if(cancel.load())break;
    auto* service=client->getService(NimBLEUUID(p.service));if(!service)continue;
    auto* chr=service->getCharacteristic(NimBLEUUID(p.characteristic));if(!chr)continue;found=true;
    if(!chr->canWrite()&&!chr->canWriteNoResponse()){message("Sound characteristic not writable");break;}
    if(chr->canIndicate()||chr->canNotify())chr->subscribe(!chr->canIndicate(),[](NimBLERemoteCharacteristic* c,uint8_t* d,size_t n,bool){value(c->getHandle(),d,n,true);});
    bool ok=!cancel.load()&&chr->writeValue(p.bytes,p.length,chr->canWrite());message(ok?"Sound command sent; listen":"Sound write rejected");
    xSemaphoreTake(mutex,portMAX_DELAY);status.soundSent=ok;xSemaphoreGive(mutex);
    uint32_t start=millis();while(ok&&!cancel.load()&&client->isConnected()&&uint32_t(millis()-start)<2000)vTaskDelay(pdMS_TO_TICKS(20));break;
  }
  if(!found&&!cancel.load())message("No supported sound service");
  if(client->isConnected())client->disconnect();
}
static void worker(void*){
  Job job;
  for(;;){
    if(xQueueReceive(jobs,&job,pdMS_TO_TICKS(100))!=pdTRUE){
      if(cancel.exchange(false)){if(client->isConnected())client->disconnect();message("Disconnected");xSemaphoreTake(mutex,portMAX_DELAY);status.busy=false;xSemaphoreGive(mutex);}
      xSemaphoreTake(mutex,portMAX_DELAY);status.connected=client->isConnected();xSemaphoreGive(mutex);continue;
    }
    if(cancel.load())continue;
    xSemaphoreTake(mutex,portMAX_DELAY);status.busy=true;xSemaphoreGive(mutex);
    if(job.op==1||job.op==6){
      if(client->isConnected())client->disconnect();
      xSemaphoreTake(mutex,portMAX_DELAY);status={};status.busy=true;xSemaphoreGive(mutex);message("Connecting...");
      // This constructor accepts display-order bytes and reverses them internally.
      if(client->connect(NimBLEAddress(job.mac,job.type),true,false,false)&&!cancel.load()){
        if(job.op==6)trackerSound();else{
        message("Discovering services...");
        unsigned count=0;bool full=false;
        for(auto* service:client->getServices(true)){
          if(cancel.load()||count>=48){full=true;break;}
          for(auto* chr:service->getCharacteristics(true)){
            if(cancel.load()||count>=48){full=true;break;}
            GattRow row;row.handle=chr->getHandle();row.flags=(chr->canRead()?1:0)|(chr->canWrite()?2:0)|(chr->canNotify()?4:0)|(chr->canIndicate()?8:0)|(chr->canWriteNoResponse()?16:0);
            snprintf(row.service,sizeof(row.service),"%s",service->getUUID().toString().c_str());snprintf(row.uuid,sizeof(row.uuid),"%s",chr->getUUID().toString().c_str());
            xSemaphoreTake(mutex,portMAX_DELAY);rows[count++]=row;status.rows=count;xSemaphoreGive(mutex);
          }
        }
        xSemaphoreTake(mutex,portMAX_DELAY);status.truncated=full;xSemaphoreGive(mutex);
        message(cancel.load()?"Cancelled":full?"Connected; first 48 rows":"Connected; A reads");
        }
      }else{char error[48];snprintf(error,sizeof(error),"Connect failed/stopped (%d)",client->getLastError());message(error);}
    }else if(client->isConnected()){
      auto* chr=client->getCharacteristic(job.handle);
      if(!chr)message("Handle not in GATT cache");
      else if(job.op==2){
        if(!chr->canRead())message("Characteristic not readable");
        else{time_t stamp=-1;auto v=chr->readValue(&stamp);if(stamp!=-1)value(job.handle,v.data(),v.size(),false);message(stamp==-1?"Read failed; may need pairing":"Value read");}
      }else if(job.op==3){
        if((job.response&&!chr->canWrite())||(!job.response&&!chr->canWriteNoResponse()))message("Write property unavailable");
        else message(chr->writeValue(job.bytes,job.size,job.response)?"Write completed":"Write failed");
      }else if(job.op==4){
        if(!chr->canNotify()&&!chr->canIndicate())message("No notify/indicate property");
        else{bool ok=chr->subscribe(chr->canNotify(),[](NimBLERemoteCharacteristic* c,uint8_t* data,size_t n,bool){value(c->getHandle(),data,n,true);});
          if(ok){xSemaphoreTake(mutex,portMAX_DELAY);for(unsigned i=0;i<status.rows;++i)if(rows[i].handle==job.handle)rows[i].flags|=32;xSemaphoreGive(mutex);}message(ok?"Subscribed; D unsubscribes":"Subscription failed");}
      }else if(job.op==5){bool ok=chr->unsubscribe();if(ok){xSemaphoreTake(mutex,portMAX_DELAY);for(unsigned i=0;i<status.rows;++i)if(rows[i].handle==job.handle)rows[i].flags&=~32;xSemaphoreGive(mutex);}message(ok?"Unsubscribed":"Unsubscribe failed");}
    }else message("Disconnected; choose target");
    if(cancel.exchange(false)){if(client->isConnected())client->disconnect();message("Disconnected");}
    xSemaphoreTake(mutex,portMAX_DELAY);status.connected=client->isConnected();status.busy=false;xSemaphoreGive(mutex);
  }
}
static bool prepare(){
  if(jobs)return true;if(!radioPrepare())return false;
  mutex=xSemaphoreCreateMutex();jobs=xQueueCreate(4,sizeof(Job));
  if(!mutex||!jobs){if(mutex)vSemaphoreDelete(mutex);if(jobs)vQueueDelete(jobs);mutex=nullptr;jobs=nullptr;return false;}
  client=NimBLEDevice::createClient();if(!client){vSemaphoreDelete(mutex);vQueueDelete(jobs);mutex=nullptr;jobs=nullptr;return false;}
  client->setConnectTimeout(4000);
  if(xTaskCreate(worker,"hp-gatt",6144,nullptr,1,nullptr)!=pdPASS){NimBLEDevice::deleteClient(client);client=nullptr;vSemaphoreDelete(mutex);vQueueDelete(jobs);mutex=nullptr;jobs=nullptr;return false;}
  return true;
}
static bool queue(Job& job){
  if(!jobs||cancel.load())return false;
  xSemaphoreTake(mutex,portMAX_DELAY);
  if(status.busy||(job.op!=1&&job.op!=6&&!status.connected)){xSemaphoreGive(mutex);return false;}
  bool ok=xQueueSend(jobs,&job,0)==pdTRUE;if(ok)status.busy=true;xSemaphoreGive(mutex);return ok;
}
bool gattConnect(const uint8_t mac[6],uint8_t type){
  if(networkStatus().busy)return false;
  if(type>1||!prepare()||gattStatus().busy||gattStatus().connected)return false;
  radioStop();cancel=false;Job job;job.op=1;memcpy(job.mac,mac,6);job.type=type;return queue(job);
}
bool gattTrackerSound(const uint8_t mac[6],uint8_t type){
  if(networkStatus().busy)return false;
  if(type>1||!prepare()||gattStatus().busy||gattStatus().connected)return false;
  radioStop();cancel=false;Job job;job.op=6;memcpy(job.mac,mac,6);job.type=type;return queue(job);
}
void gattStop(){if(!jobs)return;cancel=true;xQueueReset(jobs);client->cancelConnect();if(client->isConnected())client->disconnect();}
bool gattRead(uint16_t handle){Job j;j.op=2;j.handle=handle;return queue(j);}
bool gattWrite(uint16_t handle,const uint8_t* data,size_t n,bool response){if(!n||n>64)return false;Job j;j.op=3;j.handle=handle;j.size=n;j.response=response;memcpy(j.bytes,data,n);return queue(j);}
bool gattSubscribe(uint16_t handle,bool enable){Job j;j.op=enable?4:5;j.handle=handle;return queue(j);}
GattStatus gattStatus(){if(!mutex)return status;xSemaphoreTake(mutex,portMAX_DELAY);auto copy=status;xSemaphoreGive(mutex);return copy;}
bool gattRow(unsigned i,GattRow& row){if(!mutex)return false;xSemaphoreTake(mutex,portMAX_DELAY);bool ok=i<status.rows;if(ok)row=rows[i];xSemaphoreGive(mutex);return ok;}
}
