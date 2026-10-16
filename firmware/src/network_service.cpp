#include "network_service.h"
#include "network_format.h"
#include "radio_service.h"
#include "mp24_board.h"
#include "attack_service.h"
#include "lab_tools.h"
#include "ble_explorer.h"
#include "tracker_service.h"
#include "upload_service.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <ping/ping_sock.h>
#include <lwip/sockets.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <errno.h>
#include <atomic>

namespace hackerphone {
const char* networkProbeNames[]={"Ping host","Ping local subnet","ARP local subnet","Common TCP ports","All TCP ports 1-65535","Telnet TCP 23","SSH TCP 22","DNS TCP 53","HTTP TCP 80","HTTPS TCP 443","SMTP TCP 25","RDP TCP 3389"};
static SemaphoreHandle_t mutex=nullptr;
static QueueHandle_t jobs=nullptr;
struct Form {uint32_t at=0,address=0;uint16_t size=0;uint8_t body[512]{};};
static QueueHandle_t forms=nullptr;
static esp_netif_t* station=nullptr;static esp_netif_t* accessPoint=nullptr;
static bool tethered=false;
static NetworkStatus status;
static char rows[64][64]{};
static std::atomic<bool> cancel{false},portalRunning{false};
static std::atomic<bool> portalTesting{false};
static uint32_t joiningAt=0;
static char* portalPage=nullptr;static size_t portalPageSize=0;
static bool ready=false;
struct Job {NetworkProbe mode;uint32_t host;bool portal=false,selftest=false;};
static void message(const char* text){xSemaphoreTake(mutex,portMAX_DELAY);snprintf(status.message,sizeof(status.message),"%s",text);xSemaphoreGive(mutex);}
static void row(const char* text){xSemaphoreTake(mutex,portMAX_DELAY);++status.found;if(status.rows<64)snprintf(rows[status.rows++],64,"%s",text);else status.truncated=true;xSemaphoreGive(mutex);}
static void tested(){xSemaphoreTake(mutex,portMAX_DELAY);++status.tested;xSemaphoreGive(mutex);}
static void event(void*,esp_event_base_t base,int32_t id,void* data){
  if(!mutex)return;xSemaphoreTake(mutex,portMAX_DELAY);
  if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){auto* ip=static_cast<ip_event_got_ip_t*>(data);status.connected=true;status.connecting=false;
    status.address=ntohl(ip->ip_info.ip.addr);status.mask=ntohl(ip->ip_info.netmask.addr);status.gateway=ntohl(ip->ip_info.gw.addr);snprintf(status.message,sizeof(status.message),"Connected / DHCP ready");}
  if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED&&!(status.connecting&&static_cast<wifi_event_sta_disconnected_t*>(data)->reason==WIFI_REASON_ASSOC_LEAVE)){status.connected=false;status.connecting=false;status.address=status.mask=status.gateway=0;
    status.reason=static_cast<wifi_event_sta_disconnected_t*>(data)->reason;snprintf(status.message,sizeof(status.message),"Disconnected: reason %d",status.reason);cancel=true;}
  xSemaphoreGive(mutex);
}
static int connectSocket(uint32_t host,uint16_t port){
  int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(fd<0)return -1;
  fcntl(fd,F_SETFL,O_NONBLOCK);sockaddr_in dest{};dest.sin_family=AF_INET;dest.sin_port=htons(port);dest.sin_addr.s_addr=htonl(host);
  bool ok=connect(fd,reinterpret_cast<sockaddr*>(&dest),sizeof(dest))==0;
  if(!ok&&errno==EINPROGRESS){fd_set set;FD_ZERO(&set);FD_SET(fd,&set);timeval wait{0,180000};
    if(select(fd+1,nullptr,&set,nullptr,&wait)>0){int err=-1;socklen_t length=sizeof(err);ok=getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&length)==0&&err==0;}}
  if(!ok){close(fd);return -1;}fcntl(fd,F_SETFL,0);return fd;
}
static bool connectPort(uint32_t host,uint16_t port){int fd=connectSocket(host,port);if(fd<0)return false;close(fd);return true;}
// Eight bounded, concurrent connects share one deadline. A silent host no longer
// costs one complete timeout for each of its 65,535 ports. All FDs close on cancel.
static void connectBatch(uint32_t host,const uint16_t* ports,unsigned count){
  if(!ports||!count||count>8)return;
  int sockets[8];bool pending[8]{};uint32_t started=millis();char ip[16];formatIpv4(host,ip);
  auto found=[&](unsigned i){char s[64];snprintf(s,sizeof(s),"%s TCP %u open",ip,ports[i]);row(s);};
  for(unsigned i=0;i<count;++i){sockets[i]=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(sockets[i]<0)continue;
    fcntl(sockets[i],F_SETFL,O_NONBLOCK);sockaddr_in dest{};dest.sin_family=AF_INET;dest.sin_port=htons(ports[i]);dest.sin_addr.s_addr=htonl(host);
    int result=connect(sockets[i],reinterpret_cast<sockaddr*>(&dest),sizeof(dest));if(result==0)found(i);else pending[i]=errno==EINPROGRESS;}
  while(!cancel.load()&&uint32_t(millis()-started)<180){fd_set set;FD_ZERO(&set);int maximum=-1;
    for(unsigned i=0;i<count;++i)if(pending[i]){FD_SET(sockets[i],&set);maximum=std::max(maximum,sockets[i]);}if(maximum<0)break;
    timeval wait{0,10000};int ready=select(maximum+1,nullptr,&set,nullptr,&wait);if(ready<0)break;
    for(unsigned i=0;i<count;++i)if(pending[i]&&FD_ISSET(sockets[i],&set)){int err=-1;socklen_t length=sizeof(err);if(getsockopt(sockets[i],SOL_SOCKET,SO_ERROR,&err,&length)==0&&err==0)found(i);pending[i]=false;}}
  for(unsigned i=0;i<count;++i){if(sockets[i]>=0)close(sockets[i]);tested();}
}
struct PingContext {SemaphoreHandle_t finished;bool replied=false;uint32_t time=0;};
static bool pingHost(uint32_t host,unsigned count){
  PingContext ctx{xSemaphoreCreateBinary()};if(!ctx.finished)return false;
  esp_ping_config_t config=ESP_PING_DEFAULT_CONFIG();config.count=count;config.timeout_ms=250;config.interval_ms=300;config.data_size=16;
  IP_ADDR4(&config.target_addr,host>>24,(host>>16)&255,(host>>8)&255,host&255);
  esp_ping_callbacks_t callbacks{};callbacks.cb_args=&ctx;
  callbacks.on_ping_success=[](esp_ping_handle_t handle,void* arg){auto* c=static_cast<PingContext*>(arg);c->replied=true;esp_ping_get_profile(handle,ESP_PING_PROF_TIMEGAP,&c->time,sizeof(c->time));};
  callbacks.on_ping_end=[](esp_ping_handle_t,void* arg){xSemaphoreGive(static_cast<PingContext*>(arg)->finished);};
  esp_ping_handle_t handle=nullptr;
  if(esp_ping_new_session(&config,&callbacks,&handle)!=ESP_OK){vSemaphoreDelete(ctx.finished);return false;}
  bool started=esp_ping_start(handle)==ESP_OK;
  if(started){while(xSemaphoreTake(ctx.finished,pdMS_TO_TICKS(50))!=pdTRUE){if(cancel.load())esp_ping_stop(handle);}}
  bool reply=started&&ctx.replied;esp_ping_delete_session(handle);vSemaphoreDelete(ctx.finished);
  if(reply){char ip[16],text[64];formatIpv4(host,ip);snprintf(text,sizeof(text),"%s ICMP reply %lums",ip,(unsigned long)ctx.time);row(text);}return reply;
}
struct ArpQuery {uint32_t host;bool found=false;uint8_t mac[6]{};};
static esp_err_t arpRequest(void* context){auto* q=static_cast<ArpQuery*>(context);auto* n=static_cast<netif*>(esp_netif_get_netif_impl(station));if(!n)return ESP_FAIL;ip4_addr_t ip{htonl(q->host)};return etharp_request(n,&ip)==ERR_OK?ESP_OK:ESP_FAIL;}
static esp_err_t arpRead(void* context){auto* q=static_cast<ArpQuery*>(context);auto* n=static_cast<netif*>(esp_netif_get_netif_impl(station));if(!n)return ESP_FAIL;ip4_addr_t ip{htonl(q->host)};eth_addr* mac=nullptr;const ip4_addr_t* found=nullptr;
  if(etharp_find_addr(n,&ip,&mac,&found)>=0&&mac){q->found=true;memcpy(q->mac,mac->addr,6);}return ESP_OK;}
static void arpHost(uint32_t host){ArpQuery query{host};esp_netif_tcpip_exec(arpRequest,&query);
  for(unsigned i=0;i<4&&!cancel.load();++i){vTaskDelay(pdMS_TO_TICKS(30));esp_netif_tcpip_exec(arpRead,&query);if(query.found)break;}
  if(query.found){char ip[16],text[64];formatIpv4(host,ip);auto* m=query.mac;snprintf(text,sizeof(text),"%s %02X:%02X:%02X:%02X:%02X:%02X",ip,m[0],m[1],m[2],m[3],m[4],m[5]);row(text);}}
static int serverSocket(int type,int port){int fd=socket(AF_INET,type,0);if(fd<0)return -1;int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
  sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(port);local.sin_addr.s_addr=htonl(INADDR_ANY);
  if(bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local))<0||(type==SOCK_STREAM&&listen(fd,2)<0)){close(fd);return -1;}fcntl(fd,F_SETFL,O_NONBLOCK);return fd;}
static bool sendAll(int fd,const char* p,size_t n){size_t sent=0;while(sent<n&&!cancel.load()){int result=send(fd,p+sent,std::min<size_t>(1024,n-sent),0);if(result<=0)return false;sent+=result;}return sent==n;}
static void servePortal(){
  int dns=serverSocket(SOCK_DGRAM,53),http=serverSocket(SOCK_STREAM,80);
  if(dns<0||http<0){if(dns>=0)close(dns);if(http>=0)close(http);message("Portal sockets failed");portalRunning=false;return;}
  uint8_t query[512],reply[528];char request[1536];message("Portal: 192.168.4.1");
  while(!cancel.load()&&portalRunning.load()){
    sockaddr_in peer{};socklen_t size=sizeof(peer);int n=recvfrom(dns,query,sizeof(query),0,reinterpret_cast<sockaddr*>(&peer),&size);
    if(n>0){size_t count=portalDnsReply(query,n,reply,sizeof(reply),0xc0a80401);if(count)sendto(dns,reply,count,0,reinterpret_cast<sockaddr*>(&peer),size);}
    size=sizeof(peer);int client=accept(http,reinterpret_cast<sockaddr*>(&peer),&size);
    if(client>=0){timeval timeout{0,250000};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
      size_t received=0;PortalRequest parsed;HttpParse result=HttpParse::Incomplete;uint32_t started=millis();
      while(result==HttpParse::Incomplete&&received<sizeof(request)&&!cancel.load()&&uint32_t(millis()-started)<1000){
        n=recv(client,request+received,sizeof(request)-received,0);if(n<=0)break;received+=n;result=parsePortalRequest(request,received,parsed);}
      bool accepted=result==HttpParse::Ready;
      if(accepted&&parsed.post){Form form;form.at=millis();form.address=ntohl(peer.sin_addr.s_addr);form.size=parsed.bodySize;memcpy(form.body,request+parsed.bodyOffset,form.size);
        accepted=forms&&xQueueSend(forms,&form,0)==pdTRUE;xSemaphoreTake(mutex,portMAX_DELAY);if(accepted)++status.forms;else ++status.formsLost;xSemaphoreGive(mutex);}
      if(accepted){xSemaphoreTake(mutex,portMAX_DELAY);++status.visits;xSemaphoreGive(mutex);
        char header[180];int z=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",unsigned(portalPageSize));
        if(sendAll(client,header,z))sendAll(client,portalPage,portalPageSize);}
      else{const char* error=result==HttpParse::TooLarge?"HTTP/1.1 413 Payload Too Large\r\n":result==HttpParse::Ready?"HTTP/1.1 503 Service Unavailable\r\n":"HTTP/1.1 400 Bad Request\r\n";sendAll(client,error,strlen(error));const char* tail="Content-Length: 0\r\nConnection: close\r\n\r\n";sendAll(client,tail,strlen(tail));}
      close(client);}
    vTaskDelay(pdMS_TO_TICKS(5));
  }close(dns);close(http);portalRunning=false;
}
static void worker(void*){
  Job job;for(;;){if(xQueueReceive(jobs,&job,portMAX_DELAY)!=pdTRUE)continue;
    if(job.selftest){
      int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(0x7f000001);socklen_t size=sizeof(local);bool setup=fd>=0&&bind(fd,reinterpret_cast<sockaddr*>(&local),size)==0&&listen(fd,1)==0&&getsockname(fd,reinterpret_cast<sockaddr*>(&local),&size)==0;
      bool tcp=setup&&connectPort(0x7f000001,ntohs(local.sin_port));
      if(fd>=0)fcntl(fd,F_SETFL,O_NONBLOCK);
      int accepted=tcp?accept(fd,nullptr,nullptr):-1;if(accepted>=0)close(accepted);
      // Exercise the same batch used by full-port scans against open/closed ports.
      uint16_t ports[]={ntohs(local.sin_port)};auto before=networkStatus();if(setup)connectBatch(0x7f000001,ports,1);auto after=networkStatus();
      bool batch=setup&&after.found==before.found+1&&after.tested==before.tested+1;
      if(fd>=0)close(fd);bool closed=setup&&!connectPort(0x7f000001,ports[0]);before=networkStatus();if(setup)connectBatch(0x7f000001,ports,1);after=networkStatus();
      batch=batch&&after.found==before.found&&after.tested==before.tested+1;
      row(tcp?"PASS TCP loopback open":"FAIL TCP loopback open");row(closed?"PASS TCP closed port":"FAIL TCP closed port");row(batch?"PASS TCP batch open / closed":"FAIL TCP batch");bool ping=pingHost(0x7f000001,1);
      message(tcp&&closed&&batch&&ping?"PASS loopback TCP / batch / ICMP":"FAIL loopback self-test");
    }else if(job.portal)servePortal();else{
      auto current=networkStatus();uint32_t first=job.host,last=job.host;
      if(job.mode==NetworkProbe::Arp||job.mode==NetworkProbe::PingSweep||job.mode>=NetworkProbe::Telnet){if(!subnetRange(current.address,current.mask,first,last)){message("Subnet must be /24 to /30");cancel=true;}}
      if(job.mode==NetworkProbe::Ping||job.mode==NetworkProbe::PingSweep||job.mode==NetworkProbe::Arp){
        for(uint32_t ip=first;ip<=last&&!cancel.load();++ip){if(ip==current.address)continue;
          if(job.mode==NetworkProbe::Arp)arpHost(ip);else pingHost(ip,job.mode==NetworkProbe::Ping?4:1);tested();}}
      else if(job.mode>=NetworkProbe::Telnet){const uint16_t ports[]={23,22,53,80,443,25,3389};uint16_t port=ports[unsigned(job.mode)-unsigned(NetworkProbe::Telnet)];
        for(uint32_t ip=first;ip<=last&&!cancel.load();++ip){if(ip==current.address)continue;if(connectPort(ip,port)){char address[16],text[64];formatIpv4(ip,address);snprintf(text,sizeof(text),"%s TCP %u open",address,port);row(text);}tested();vTaskDelay(1);}}
      else{
        const uint16_t common[]={21,22,23,25,53,80,110,135,139,143,443,445,465,587,993,995,1883,3306,3389,5432,5900,8000,8080,8443};
        const uint16_t service[]={23,22,53,80,443,25,3389};
        unsigned count=job.mode==NetworkProbe::AllPorts?65535:job.mode==NetworkProbe::CommonPorts?sizeof(common)/sizeof(*common):1;
        for(unsigned i=0;i<count&&!cancel.load();i+=8){uint16_t ports[8];unsigned n=std::min(8u,count-i);for(unsigned j=0;j<n;++j)ports[j]=job.mode==NetworkProbe::AllPorts?i+j+1:job.mode==NetworkProbe::CommonPorts?common[i+j]:service[unsigned(job.mode)-unsigned(NetworkProbe::Telnet)];connectBatch(job.host,ports,n);vTaskDelay(1);}
      }
      message(cancel.load()?"Scan cancelled":"Scan complete (open != identity)");
    }
    xSemaphoreTake(mutex,portMAX_DELAY);status.busy=false;status.portal=false;xSemaphoreGive(mutex);
  }
}
static bool prepare(){
  if(ready)return true;if(jobs)return false;if(!radioPrepare(false))return false;
  mutex=xSemaphoreCreateMutex();jobs=xQueueCreate(1,sizeof(Job));forms=xQueueCreate(4,sizeof(Form));
  if(!mutex||!jobs||!forms)return false;
  station=esp_netif_create_default_wifi_sta();accessPoint=esp_netif_create_default_wifi_ap();
  if(!station||!accessPoint)return false;
  esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,event,nullptr);esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,nullptr);
  ready=xTaskCreate(worker,"hp-network",8192,nullptr,1,nullptr)==pdPASS;return ready;
}
NetworkStatus networkStatus(){if(!mutex)return status;xSemaphoreTake(mutex,portMAX_DELAY);auto result=status;xSemaphoreGive(mutex);result.tethered=tethered;
  auto iface=result.hotspot?accessPoint:station;if(iface){result.interfaceUp=esp_netif_is_netif_up(iface);if(result.hotspot){esp_netif_ip_info_t ip{};if(esp_netif_get_ip_info(iface,&ip)==ESP_OK)result.address=ntohl(ip.ip.addr);}}return result;}
bool networkRow(unsigned index,char* out,size_t cap){if(!mutex||!out||!cap)return false;xSemaphoreTake(mutex,portMAX_DELAY);bool ok=index<status.rows;if(ok)snprintf(out,cap,"%s",rows[index]);xSemaphoreGive(mutex);return ok;}
void networkCancelProbe(){cancel=true;portalRunning=false;uploadCancel();}
bool networkClaimExternal(){if(!mutex)return false;xSemaphoreTake(mutex,portMAX_DELAY);bool ok=status.connected&&!status.busy;if(ok)status.busy=true;xSemaphoreGive(mutex);return ok;}
void networkReleaseExternal(const char* text){if(!mutex)return;xSemaphoreTake(mutex,portMAX_DELAY);status.busy=false;snprintf(status.message,sizeof(status.message),"%s",text);xSemaphoreGive(mutex);}
void networkStop(){networkCancelProbe();networkTether(false);if(!mutex)return;auto s=networkStatus();if(s.connected||s.connecting)esp_wifi_disconnect();if(s.connected||s.connecting||s.hotspot)esp_wifi_set_mode(WIFI_MODE_NULL);xSemaphoreTake(mutex,portMAX_DELAY);status.connecting=status.connected=status.hotspot=status.portal=false;status.address=status.mask=status.gateway=0;xSemaphoreGive(mutex);}
bool networkTether(bool enable,uint32_t dns){
 if(!enable){if(tethered&&accessPoint)esp_netif_napt_disable(accessPoint);tethered=false;return true;}
 auto s=networkStatus();if(!s.hotspot||s.portal||!s.interfaceUp||!accessPoint||!dns)return false;
 if(!tethered){esp_netif_dhcps_stop(accessPoint);esp_netif_dns_info_t info{};info.ip.type=ESP_IPADDR_TYPE_V4;info.ip.u_addr.ip4.addr=htonl(dns);esp_netif_set_dns_info(accessPoint,ESP_NETIF_DNS_MAIN,&info);uint8_t offer=1;esp_netif_dhcps_option(accessPoint,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offer,sizeof(offer));esp_netif_dhcps_start(accessPoint);if(esp_netif_napt_enable(accessPoint)!=ESP_OK)return false;tethered=true;message("Hotspot shares cellular data");}return true;
}
bool networkJoin(const char* ssid,const char* password){
  if(attackStatus.active||lab.mode!=LabMode::Off||gattStatus().busy||gattStatus().connected||trackerLive.active)return false;
  if(!ssid||!*ssid||strlen(ssid)>32||!password||strlen(password)>63||!prepare()||networkStatus().busy)return false;
  networkStop();radioStop();wifi_config_t config{};memcpy(config.sta.ssid,ssid,strlen(ssid));memcpy(config.sta.password,password,strlen(password));config.sta.pmf_cfg.capable=true;
  if(esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK||esp_wifi_set_config(WIFI_IF_STA,&config)!=ESP_OK){message("WiFi join failed");return false;}
  xSemaphoreTake(mutex,portMAX_DELAY);status.connecting=true;snprintf(status.message,sizeof(status.message),"Connecting (20s timeout)");xSemaphoreGive(mutex);joiningAt=millis();cancel=false;
  if(esp_wifi_connect()!=ESP_OK){networkStop();message("WiFi join failed");return false;}return true;
}
bool networkHotspot(const char* ssid,const char* password,bool portal){
  if(attackStatus.active||lab.mode!=LabMode::Off||gattStatus().busy||gattStatus().connected||trackerLive.active)return false;
  if(!ssid||!*ssid||strlen(ssid)>32||!password||(strlen(password)&&strlen(password)<8)||strlen(password)>63||!prepare()||networkStatus().busy)return false;
  if(portal){if(!portalPage)portalPage=static_cast<char*>(ps_malloc(16385));if(!portalPage)return false;
    const char* page="<!doctype html><meta name=viewport content='width=device-width'><title>HackerPhone lab</title><h1>HackerPhone network demo</h1><p>Local training hotspot. No Internet uplink. Demo submissions are stored on this device.</p><form method=post><label>Test message <input name=demo maxlength=120></label><button>Submit demo</button></form>";
    portalPageSize=strlen(page);memcpy(portalPage,page,portalPageSize+1);
    if(mp24::cardReady){File file=SD.open("/hackerphone/portal/index.html",FILE_READ);if(file&&!file.isDirectory()&&file.size()>0&&file.size()<=16384){size_t wanted=file.size(),got=file.readBytes(portalPage,wanted);if(got!=wanted){file.close();return false;}portalPageSize=got;portalPage[got]=0;}file.close();}}
  networkStop();radioStop();wifi_config_t config{};memcpy(config.ap.ssid,ssid,strlen(ssid));config.ap.ssid_len=strlen(ssid);memcpy(config.ap.password,password,strlen(password));config.ap.channel=1;config.ap.max_connection=4;config.ap.authmode=*password?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
  if(esp_wifi_set_mode(WIFI_MODE_AP)!=ESP_OK||esp_wifi_set_config(WIFI_IF_AP,&config)!=ESP_OK){message("Hotspot setup failed");return false;}
  cancel=false;xSemaphoreTake(mutex,portMAX_DELAY);status.hotspot=true;status.address=0xc0a80401;status.portal=status.busy=portal;status.visits=0;status.forms=status.formsSaved=status.formsLost=0;status.rows=0;status.tested=status.found=0;status.truncated=false;snprintf(status.message,sizeof(status.message),"Hotspot 192.168.4.1");xSemaphoreGive(mutex);
  if(portal){portalRunning=true;Job job{};job.portal=true;if(xQueueSend(jobs,&job,0)!=pdTRUE){networkStop();return false;}}return true;
}
bool networkProbe(NetworkProbe mode,const char* host){
  if(unsigned(mode)>=unsigned(NetworkProbe::Count)||!prepare())return false;auto s=networkStatus();uint32_t ip=0;
  if(!s.connected||s.busy)return false;
  if(mode!=NetworkProbe::Arp&&mode!=NetworkProbe::PingSweep&&(!parseIpv4(host,ip)||!ip||ip>=0xe0000000))return false;
  cancel=false;xSemaphoreTake(mutex,portMAX_DELAY);status.busy=true;status.rows=0;status.tested=status.found=0;status.truncated=false;snprintf(status.message,sizeof(status.message),"%s",networkProbeNames[unsigned(mode)]);xSemaphoreGive(mutex);
  Job job{mode,ip,false};if(xQueueSend(jobs,&job,0)==pdTRUE)return true;xSemaphoreTake(mutex,portMAX_DELAY);status.busy=false;xSemaphoreGive(mutex);return false;
}
void networkTick(uint32_t now){auto s=networkStatus();if(s.connecting&&uint32_t(now-joiningAt)>20000){networkStop();message("WiFi join timed out");}
  Form form;if(forms&&xQueueReceive(forms,&form,0)==pdTRUE){bool ok=mp24::cardReady&&(SD.exists("/hackerphone")||SD.mkdir("/hackerphone"))&&(SD.exists("/hackerphone/portal")||SD.mkdir("/hackerphone/portal"));
    if(ok){File out=SD.open("/hackerphone/portal/submissions.csv",FILE_APPEND);ok=bool(out);if(ok){char ip[16];formatIpv4(form.address,ip);ok=out.printf("%lu,%s,",(unsigned long)form.at,ip)>0;
        const char* hex="0123456789ABCDEF";for(unsigned i=0;i<form.size;++i){char encoded[]={hex[form.body[i]>>4],hex[form.body[i]&15]};if(out.write(reinterpret_cast<uint8_t*>(encoded),2)!=2)ok=false;}if(out.write('\n')!=1)ok=false;out.flush();}out.close();}
    xSemaphoreTake(mutex,portMAX_DELAY);if(ok)++status.formsSaved;else ++status.formsLost;xSemaphoreGive(mutex);}}
bool networkSelfTest(){if(!prepare())return false;auto s=networkStatus();if(s.busy||s.connected||s.connecting||s.hotspot||attackStatus.active||lab.mode!=LabMode::Off||trackerLive.active||gattStatus().busy||gattStatus().connected)return false;
  radioStop();cancel=false;xSemaphoreTake(mutex,portMAX_DELAY);status.busy=true;status.rows=0;status.tested=status.found=0;status.truncated=false;xSemaphoreGive(mutex);Job job{};job.selftest=true;
  if(xQueueSend(jobs,&job,0)==pdTRUE)return true;xSemaphoreTake(mutex,portMAX_DELAY);status.busy=false;xSemaphoreGive(mutex);return false;}
static void portalTestWorker(void*){
  // Requests stay on this ESP32's loopback interface. This exercises the real
  // socket service and SD queue, without connecting a station or a nearby host.
  sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(0x7f000001);local.sin_port=htons(80);timeval timeout{1,0};
  const char* requests[]={"GET / HTTP/1.1\r\nHost: selftest\r\n\r\n","POST / HTTP/1.1\r\nHost: selftest\r\nContent-Length: 22\r\n\r\ndemo=HP-SELFTEST-LOCAL"};
  for(unsigned i=0;i<2&&!cancel.load();++i){int fd=connectSocket(0x7f000001,80);bool ok=fd>=0;
    if(ok){setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
      if(ok){size_t n=strlen(requests[i]);ok=sendAll(fd,requests[i],10);vTaskDelay(pdMS_TO_TICKS(20));ok=ok&&sendAll(fd,requests[i]+10,n-10);char reply[128];int received=recv(fd,reply,sizeof(reply),0);ok=ok&&received>=15&&!memcmp(reply,"HTTP/1.1 200 OK",15);}close(fd);}
    row(i==0?(ok?"PASS local HTTP GET":"FAIL local HTTP GET"):(ok?"PASS local HTTP POST":"FAIL local HTTP POST"));}
  int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);bool ok=fd>=0;if(ok){setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));local.sin_port=htons(53);
    uint8_t query[]={0x48,0x50,1,0,0,1,0,0,0,0,0,0,4,'d','e','m','o',0,0,1,0,1},reply[64];
    ok=sendto(fd,query,sizeof(query),0,reinterpret_cast<sockaddr*>(&local),sizeof(local))==sizeof(query);int n=recv(fd,reply,sizeof(reply),0);
    ok=ok&&n==38&&reply[0]==0x48&&reply[1]==0x50&&reply[7]==1&&!memcmp(reply+n-4,"\xc0\xa8\x04\x01",4);close(fd);}
  row(ok?"PASS local DNS A reply":"FAIL local DNS A reply");portalTesting=false;vTaskDelete(nullptr);
}
bool networkPortalTest(){if(!networkStatus().portal||portalTesting.exchange(true))return false;if(xTaskCreate(portalTestWorker,"hp-portal-test",4096,nullptr,1,nullptr)==pdPASS)return true;portalTesting=false;return false;}
bool networkSaveResults(){if(!mp24::cardReady||!mutex)return false;auto s=networkStatus();if(s.busy)return false;if(!SD.exists("/hackerphone"))SD.mkdir("/hackerphone");
  char path[80];snprintf(path,sizeof(path),"/hackerphone/network-%08lx.txt",(unsigned long)millis());if(SD.exists(path))return false;File out=SD.open(path,FILE_WRITE);if(!out)return false;
  bool ok=out.printf("Tested %lu; found %lu; rows %u; truncated %u\n",(unsigned long)s.tested,(unsigned long)s.found,s.rows,s.truncated)>0;
  char text[64];for(unsigned i=0;networkRow(i,text,sizeof(text));++i)if(out.println(text)!=strlen(text)+2)ok=false;out.flush();out.close();return ok;
}
}
