#include "upload_service.h"
#include "network_service.h"
#include "mp24_board.h"
#include "music_service.h"
#include "file_service.h"
#include "device_controls.h"
#include <ArduinoJson.h>
#include <esp_http_client.h>
#include <esp_random.h>
#include <esp_crt_bundle.h>
#include <time.h>
#include <atomic>
namespace hackerphone {
static UploadStatus status;static SemaphoreHandle_t mutex=nullptr;static std::atomic<bool> cancelled{false};
static char path[160],key[256];static unsigned service=0;
static void message(const char* s){xSemaphoreTake(mutex,portMAX_DELAY);snprintf(status.message,sizeof(status.message),"%s",s);xSemaphoreGive(mutex);}
UploadStatus uploadStatus(){if(mutex)xSemaphoreTake(mutex,portMAX_DELAY);auto s=status;if(mutex)xSemaphoreGive(mutex);return s;}
void uploadCancel(){cancelled=true;}
static bool writeAll(esp_http_client_handle_t client,const char* data,unsigned n){unsigned at=0;while(at<n&&!cancelled.load()){int sent=esp_http_client_write(client,data+at,n-at);if(sent<=0)return false;at+=sent;}return at==n;}
static void worker(void*){
 bool ok=false;int http=0;File file;esp_http_client_handle_t client=nullptr;char* buffer=static_cast<char*>(ps_malloc(4096));
 if(!buffer){message("No upload buffer");goto finish;}
 // Certificate verification remains enabled. Synchronize time only on an explicit upload.
 if(time(nullptr)<1700000000){configTime(0,0,"pool.ntp.org");message("Synchronizing TLS clock");uint32_t start=millis();while(time(nullptr)<1700000000&&!cancelled.load()&&uint32_t(millis()-start)<15000)vTaskDelay(pdMS_TO_TICKS(100));}
 if(time(nullptr)<1700000000||cancelled.load()){message("Clock unavailable / cancelled");goto finish;}
 file=SD.open(path);if(!file){message("File unavailable");goto finish;}
 {
   esp_http_client_config_t cfg{};cfg.url=service==0?"https://api.wigle.net/api/v2/file/upload":"https://wdgwars.pl/api/v2/upload-csv";cfg.method=HTTP_METHOD_POST;cfg.timeout_ms=4000;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_redirect=true;cfg.buffer_size=1024;cfg.buffer_size_tx=1024;
   client=esp_http_client_init(&cfg);if(!client){message("TLS client unavailable");goto finish;}
   char boundary[40];snprintf(boundary,sizeof(boundary),"HackerPhone%08lx",(unsigned long)esp_random());char type[100];snprintf(type,sizeof(type),"multipart/form-data; boundary=%s",boundary);esp_http_client_set_header(client,"Content-Type",type);
   esp_http_client_set_header(client,"Accept","application/json");esp_http_client_set_header(client,"User-Agent","HackerPhone/0.5");
   if(service==0){char auth[270];snprintf(auth,sizeof(auth),"Basic %s",key);esp_http_client_set_header(client,"Authorization",auth);}else esp_http_client_set_header(client,"X-API-Key",key);
   const char* basename=strrchr(path,'/');String head=String("--")+boundary+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\""+(basename?basename+1:"scan.csv")+"\"\r\nContent-Type: text/csv\r\n\r\n";
   String tail="\r\n";if(service==0)tail+=String("--")+boundary+"\r\nContent-Disposition: form-data; name=\"donate\"\r\n\r\nfalse\r\n";tail+=String("--")+boundary+"--\r\n";
   size_t length=head.length()+file.size()+tail.length();message(service==0?"Uploading selected CSV to WiGLE":"Uploading selected CSV to WDG");
   if(esp_http_client_open(client,length)!=ESP_OK||!writeAll(client,head.c_str(),head.length())){message("TLS / connection failed");goto finish;}
   while(file.available()&&!cancelled.load()){int n=file.read(reinterpret_cast<uint8_t*>(buffer),4096);if(n<=0||!writeAll(client,buffer,n)){message("Upload interrupted; no auto retry");goto finish;}xSemaphoreTake(mutex,portMAX_DELAY);status.sent+=n;xSemaphoreGive(mutex);vTaskDelay(1);}
   if(cancelled.load()||!writeAll(client,tail.c_str(),tail.length())){message("Cancelled / outcome uncertain");goto finish;}
   if(esp_http_client_fetch_headers(client)<0){message("Response missing; outcome uncertain");goto finish;}http=esp_http_client_get_status_code(client);
   int received=esp_http_client_read_response(client,buffer,4095);if(received<0)received=0;buffer[received]=0;StaticJsonDocument<2048> response;auto parsed=deserializeJson(response,buffer,received);
   ok=(http>=200&&http<300)&&(service==1&&http==202||!parsed&&(service==0?response["success"]==true:response["ok"]==true));
   message(ok?"Server accepted upload":"Server did not confirm acceptance");
 }
 finish:
 if(client){esp_http_client_close(client);esp_http_client_cleanup(client);}file.close();free(buffer);memset(key,0,sizeof(key));
 if(ok){File receipt=SD.open(String(path)+(service==0?".wigle":".wdg"),FILE_WRITE);if(receipt){receipt.printf("accepted=true\nhttp=%d\nuptime_ms=%lu\n",http,(unsigned long)millis());receipt.close();}}
 xSemaphoreTake(mutex,portMAX_DELAY);status.busy=false;status.accepted=ok;status.http=http;xSemaphoreGive(mutex);networkReleaseExternal(ok?"Upload accepted":"Upload stopped; check result");vTaskDelete(nullptr);
}
bool uploadStart(unsigned selected,const char* filename){
 if(selected>1||!mp24::cardReady||!safeSdPath(filename)||strncmp(filename,"/hackerphone/",13)||musicStatus().busy||fileBusy())return false;
 if(!mutex){mutex=xSemaphoreCreateMutex();if(!mutex)return false;}if(uploadStatus().busy)return false;
 File file=SD.open(filename);if(!file||file.isDirectory()||file.size()>16*1024*1024){file.close();message("CSV missing / over 16 MiB");return false;}
 char header[14]{};file.readBytes(header,13);uint32_t size=file.size();file.close();if(strncmp(header,"WigleWifi-1.4",13)){message("Needs geolocated WiGLE CSV");return false;}
 File config=SD.open("/hackerphone/upload.json");if(!config||config.size()>2048){config.close();message("Configure SD upload.json first");return false;}StaticJsonDocument<2048> doc;auto error=deserializeJson(doc,config);config.close();const char* token=doc[selected==0?"wigle_basic":"wdg_key"]|"";
 if(error||!*token||strlen(token)>=sizeof(key)){message("Missing service API credential");return false;}for(const char* p=token;*p;++p)if(uint8_t(*p)<33||uint8_t(*p)>126){message("Invalid credential characters");return false;}
 if(!networkClaimExternal()){message("Join WiFi / stop network scan first");return false;}
 snprintf(path,sizeof(path),"%s",filename);snprintf(key,sizeof(key),"%s",token);service=selected;cancelled=false;
 xSemaphoreTake(mutex,portMAX_DELAY);status={};status.busy=true;status.total=size;xSemaphoreGive(mutex);
 if(xTaskCreate(worker,"hp-upload",10240,nullptr,1,nullptr)==pdPASS)return true;memset(key,0,sizeof(key));status.busy=false;networkReleaseExternal("Upload worker unavailable");return false;
}
}
