#include "cellular_service.h"
#include "mp24_board.h"
#include <esp_modem_api.h>
#include <esp_netif.h>
#include <esp_netif_defaults.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <freertos/stream_buffer.h>
#include <atomic>
#include <string.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

namespace hackerphone {
namespace {
SemaphoreHandle_t mutex=nullptr;
QueueHandle_t commands=nullptr;
StreamBufferHandle_t received=nullptr;
esp_netif_t* netif=nullptr;
CellularStatus status;
std::atomic<bool> owned{false},stop{false},linked{false},returned{false},testing{false};
struct Command {char text[384]{};};
Command pending;size_t pendingSize=0,delivered=0;
bool listening=false; // Worker-owned; callback executes synchronously within each command.
bool resumed=false;
void message(const char* text){xSemaphoreTake(mutex,portMAX_DELAY);snprintf(status.message,sizeof(status.message),"%s",text);xSemaphoreGive(mutex);}
void ipEvent(void*,esp_event_base_t,int32_t id,void* data){
 xSemaphoreTake(mutex,portMAX_DELAY);
 if(id==IP_EVENT_PPP_GOT_IP){auto* got=static_cast<ip_event_got_ip_t*>(data);if(got->esp_netif==netif){status.connected=true;status.address=ntohl(got->ip_info.ip.addr);esp_netif_dns_info_t dns{};esp_netif_get_dns_info(netif,ESP_NETIF_DNS_MAIN,&dns);status.dns=ntohl(dns.ip.u_addr.ip4.addr);snprintf(status.message,sizeof(status.message),"LTE data connected");}}
 else if(id==IP_EVENT_PPP_LOST_IP){status.connected=false;status.address=0;snprintf(status.message,sizeof(status.message),"Cellular data disconnected");}
 xSemaphoreGive(mutex);
}
// esp_modem passes an accumulating command buffer. Forward each byte only once.
esp_err_t reply(uint8_t* data,size_t length){
 if(length>delivered){size_t n=length-delivered;if(xStreamBufferSend(received,data+delivered,n,0)!=n){stop=true;return ESP_FAIL;}delivered=length;}
 if(listening)return ESP_OK;
 for(size_t i=0;i<length;++i){
  if((i==0||data[i-1]=='\n')&&i+2<length&&data[i]=='O'&&data[i+1]=='K'&&(data[i+2]=='\r'||data[i+2]=='\n'))return ESP_OK;
  if(i+5<=length&&!memcmp(data+i,"ERROR",5))return ESP_FAIL;
  if(data[i]=='>'&&(i==0||data[i-1]=='\n'))return ESP_OK;
 }
 return ESP_ERR_TIMEOUT;
}
void worker(void*){
 esp_modem_dte_config_t dte=ESP_MODEM_DTE_DEFAULT_CONFIG();dte.uart_config.tx_io_num=17;dte.uart_config.rx_io_num=18;dte.uart_config.rts_io_num=dte.uart_config.cts_io_num=-1;dte.uart_config.rx_buffer_size=8192;dte.task_stack_size=6144;
 esp_modem_dce_config_t config=ESP_MODEM_DCE_DEFAULT_CONFIG("Wholesale");config.protocol_type="IPV4V6";
 esp_modem_dce_t* modem=esp_modem_new_dev(ESP_MODEM_DCE_GENERIC,&dte,&config,netif);
 bool mux=false;
 if(modem){
  if(esp_modem_sync(modem)==ESP_OK){message("Starting LTE data / CMUX");mux=esp_modem_set_mode(modem,ESP_MODEM_MODE_CMUX)==ESP_OK;}
  if(mux){linked=true;message("Waiting for cellular IP");Command c;
   uint32_t started=millis();
   while(!stop.load()){
    if(!cellularStatus().connected&&uint32_t(millis()-started)>60000){message("Cellular IP timed out");break;}
    delivered=0;
    if(xQueueReceive(commands,&c,0)==pdTRUE){listening=false;uint32_t timeout=strchr(c.text,26)?120000:strstr(c.text,"ATD")||strstr(c.text,"ATA")?30000:15000;auto err=esp_modem_command(modem,c.text,reply,timeout);if(err==ESP_ERR_TIMEOUT){message("Cellular AT timed out");break;}}
    else {listening=true;esp_modem_command(modem,"",reply,150);}
   }
  }else message("Modem multiplexing failed");
  linked=false;
  // Exit PPP and CMUX before handing UART back to the normal phone service.
  auto exit=esp_modem_set_mode(modem,ESP_MODEM_MODE_COMMAND);
  if(exit==ESP_OK||esp_modem_sync(modem)==ESP_OK)message("Cellular data off");else message("Data stopped; resync pending");
  esp_modem_destroy(modem);
 }else message("Cellular modem allocation failed");
 xSemaphoreTake(mutex,portMAX_DELAY);status.connected=false;status.starting=false;status.stopping=false;status.active=false;status.address=0;xSemaphoreGive(mutex);
 returned=true;vTaskDelete(nullptr);
}
void testWorker(void*){
 struct ifreq iface{};esp_netif_get_netif_impl_name(netif,iface.ifr_name);
 esp_http_client_config_t config{};config.url="http://example.com/";config.timeout_ms=20000;config.buffer_size=512;config.if_name=&iface;
 esp_http_client_handle_t client=esp_http_client_init(&config);esp_err_t result=client?esp_http_client_perform(client):ESP_FAIL;int code=client?esp_http_client_get_status_code(client):0;
 char text[80];snprintf(text,sizeof(text),"Internet test: %s / HTTP %d",esp_err_to_name(result),code);message(text);if(client)esp_http_client_cleanup(client);testing=false;vTaskDelete(nullptr);
}
}
CellularStatus cellularStatus(){if(!mutex)return status;xSemaphoreTake(mutex,portMAX_DELAY);auto s=status;xSemaphoreGive(mutex);return s;}
bool cellularOwnsModem(){return owned.load();}
bool cellularStart(PhoneCore& phone){
 if(owned.load()||testing.load()||phone.busy()||phone.state().phonePriority()||!phone.state().online||!phone.state().registered())return false;
 if(!mutex)mutex=xSemaphoreCreateMutex();if(!mutex)return false;
 if(!commands)commands=xQueueCreate(2,sizeof(Command));if(!received)received=xStreamBufferCreate(4096,1);if(!commands||!received)return false;
 if(!netif){if(esp_netif_init()!=ESP_OK)return false;auto err=esp_event_loop_create_default();if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)return false;esp_netif_config_t cfg=ESP_NETIF_DEFAULT_PPP();netif=esp_netif_new(&cfg);if(!netif)return false;esp_event_handler_register(IP_EVENT,IP_EVENT_PPP_GOT_IP,ipEvent,nullptr);esp_event_handler_register(IP_EVENT,IP_EVENT_PPP_LOST_IP,ipEvent,nullptr);}
 xQueueReset(commands);xStreamBufferReset(received);pendingSize=0;pending={};stop=linked=returned=false;resumed=false;
 xSemaphoreTake(mutex,portMAX_DELAY);status={};status.active=status.starting=true;snprintf(status.message,sizeof(status.message),"Starting cellular data");xSemaphoreGive(mutex);
 phone.suspend(true);owned=true;mp24::modemReleaseUart();
 if(xTaskCreate(worker,"hp-cellular",8192,nullptr,3,nullptr)!=pdPASS){owned=false;phone.suspend(false);mp24::modemResumeUart();xSemaphoreTake(mutex,portMAX_DELAY);status.active=status.starting=false;xSemaphoreGive(mutex);message("Cellular task allocation failed");return false;}return true;
}
void cellularStop(){if(!owned.load())return;stop=true;xSemaphoreTake(mutex,portMAX_DELAY);status.stopping=true;xSemaphoreGive(mutex);}
bool cellularWrite(const char* bytes,size_t size){
 if(!linked.load()||stop.load()||!bytes||pendingSize+size>=sizeof(pending.text))return false;
 memcpy(pending.text+pendingSize,bytes,size);pendingSize+=size;pending.text[pendingSize]=0;
 if(memchr(bytes,'\r',size)||memchr(bytes,26,size)||memchr(bytes,27,size)){bool ok=xQueueSend(commands,&pending,0)==pdTRUE;pendingSize=0;pending={};return ok;}return true;
}
void cellularPoll(PhoneCore& phone,uint32_t now){
 if(!owned.load())return;
 if(returned.load()){owned=false;mp24::modemResumeUart();phone.suspend(false);phone.begin(now);return;}
 if(!linked.load()||stop.load()){phone.suspend(true);return;}
 if(!resumed){resumed=true;phone.suspend(false);phone.begin(now);xSemaphoreTake(mutex,portMAX_DELAY);status.starting=false;xSemaphoreGive(mutex);}
 uint8_t bytes[512];size_t n=xStreamBufferReceive(received,bytes,sizeof(bytes),0);if(n)phone.feed(bytes,n,now);phone.tick(now);
}
bool cellularTest(){if(!cellularStatus().connected||testing.exchange(true))return false;if(xTaskCreate(testWorker,"hp-cell-test",6144,nullptr,1,nullptr)==pdPASS)return true;testing=false;return false;}
}
