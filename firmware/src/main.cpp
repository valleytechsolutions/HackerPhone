#include "mp24_board.h"
#include "mp24_display.h"
#include "radio_service.h"
#include "capture_format.h"
#include "radar_colors.h"
#include "logo_assets.h"
#include <esp_timer.h>
#include <math.h>
#include <Preferences.h>
#include "ui_theme.h"
#include "storage_service.h"
#include "vendor_db.h"
#include "ble_explorer.h"
#include "lab_tools.h"
#include "attack_service.h"
#include "ir_service.h"
#include "network_service.h"
#include "network_format.h"
#include "saved_lists.h"
#include "tracker_service.h"
#include <esp_wifi.h>
#include <esp_sleep.h>
#include "music_service.h"
#include "led_service.h"
#include "device_controls.h"
#include "list_editor.h"
#include "mission_service.h"
#include "file_service.h"
#include "tone_service.h"
#include "upload_service.h"

using namespace hackerphone;
static mp24::ModemTransport transport;
static PhoneCore phone(transport);
enum class Screen {Home,Dial,Messages,Compose,ConfirmSms,Recon,Targets,Hunt,Files,FileView,Settings,Diagnostics,Incoming,Tools,Channels,Alerts,TargetTools,TargetInfo,Gatt,Lab,LabConfirm,Attacks,AttackConfirm,AttackRun,Infrared,IrSignal,Scanners,PacketMonitor,Network,NetEdit,NetReview,NetResults,TrackerConfirm,AttackConfig,SavedLists,TrackerLiveConfirm,TrackerLiveRun,Music,Leds,Playlist,Mission,MacSettings,FileActions,TrashConfirm,Tones,UploadReview};
static Screen screen=Screen::Home;
static PowerButton powerButton;
static bool locked=false,shuttingDown=false,cutRequested=false;
static uint32_t shutdownAt=0,volumeAt=0,volumeSaveAt=0;
static int nextMusic=0;
static bool musicBrowsing=false;static unsigned playlistIndex=0,uploadChoice=0;

static int menu=0,targetIndex=0,fileIndex=0,fileCount=0;
static String recipient,message,dialNumber,currentPath="/",filePreview;
static String fileNames[32],filePaths[32];static bool fileDirs[32];
static unsigned filePage=0;static bool fileMore=false;
static bool irBrowsing=false;
static String notice;
static uint32_t noticeAt=0,lastDraw=0,lastSave=0;
static uint8_t previousDigit=255,tapIndex=0;static uint32_t tapAt=0;
static File session;static bool logging=false;
static char sessionPath[80]{};
static bool ready=false;
static bool smsSubmitted=false;
static Preferences preferences;
static unsigned themeIndex=0;
static const Theme& theme(){return themes[themeIndex];}
static unsigned targetFilter=0,detailPage=0,gattIndex=0,eventIndex=0;
static bool handshakePage=false;
static LabMode pendingLab=LabMode::Off;
static AttackMode pendingAttack=AttackMode::Beacon;
static char vendorName[48]{};
static String networkSsid="HackerPhone-Lab",networkPassword,networkHost="192.168.4.1",networkEdit;
static unsigned networkField=0,networkSelection=0,networkResult=0;
static bool networkUpper=false,networkShowPassword=false;
static String ipText(uint32_t address){char s[16];formatIpv4(address,s);return s;}
static const char* modeName(){return radioMode==RadioMode::Combined?"WiFi + BLE":radioMode==RadioMode::Wifi?"WiFi only":"BLE only";}
static void setTheme(unsigned index){themeIndex=index%ThemeCount;preferences.putUChar("theme",themeIndex);}
static bool targetVisible(const Target& t){return scanMatches(t,ScanProfile(targetFilter));}
static void cacheVendor(){vendorName[0]=0;auto& t=recon.targets[targetIndex];if(t.used)lookupVendor(t.mac,t.ble,t.addressType,vendorName);}
static void nextTarget(int step){for(unsigned i=0;i<ReconCore::Capacity;++i){targetIndex=(targetIndex+ReconCore::Capacity+step)%ReconCore::Capacity;if(targetVisible(recon.targets[targetIndex]))break;}recon.pinned=targetVisible(recon.targets[targetIndex])?targetIndex:-1;cacheVendor();}
static bool startListening(){irStop();trackerLiveStop();attackStop();labStop();networkStop();if(networkStatus().busy)return false;auto g=gattStatus();if(g.busy||g.connected){gattStop();return false;}return radioStart();}
static void stopTools(){if(logging){session.flush();session.close();logging=false;}missionStop();irStop();trackerLiveStop();attackStop();labStop();networkStop();gattStop();radioStop();captureStop();recon.selected=recon.pinned=-1;}
static void openInfrared(){stopTools();if(logging){session.flush();session.close();logging=false;}screen=Screen::Infrared;menu=0;}

static const char* simName(){switch(phone.state().sim){case Sim::Absent:return "NO SIM";case Sim::Ready:return phone.state().registered()?"REGISTERED":"SEARCHING";case Sim::PinRequired:return "SIM PIN";case Sim::PukRequired:return "SIM PUK";case Sim::Fault:return "SIM ERROR";default:return phone.state().online?"CHECKING SIM":"MODEM START";}}
static void notify(const char* text){notice=text;noticeAt=millis();}
static uint16_t color(uint32_t c){return mp24::lcd.color565(c>>16,c>>8,c);}
static void text(int x,int y,const String& s,uint16_t fg=TFT_WHITE){mp24::frame.setTextColor(fg);mp24::frame.drawString(s,x,y,1);}
static void wrap(int x,int y,const String& s,int width=24,int maxLines=7){for(int line=0;line<maxLines&&size_t(line*width)<s.length();++line)text(x,y+line*10,s.substring(line*width,(line+1)*width));}
static void header(const char* title){auto& f=mp24::frame;auto net=networkStatus();bool tx=attackStatus.active||lab.mode!=LabMode::Off||trackerLive.active;
  const char* state=tx?"TX ACTIVE":net.portal?"PORTAL":net.hotspot?"WiFi AP":net.connecting?"JOINING":net.connected?(net.busy?"NET SCAN":"WiFi STA"):storageStatus.recording?"REC PCAP":radioRunning?(radioBle?(bleActiveScan?"BLE ASK":"BLE RX"):"WiFi RX"):"IDLE";
  f.fillSprite(theme().background);f.fillRect(0,0,160,15,theme().bar);text(5,4,title);text(5,18,simName(),theme().muted);text(102,18,state,tx||storageStatus.recording||net.hotspot?TFT_ORANGE:theme().accent);f.drawFastHLine(0,29,160,0x39e7);}
static void footer(const char* help){mp24::frame.fillRect(0,116,160,12,TFT_BLACK);text(3,118,help,theme().muted);}
static void showResult(Request result){
  const char* names[]={"Request queued","Modem busy","Insert a SIM first","No cellular network","Invalid input","70-character SMS limit","Text not supported yet"};
  notify(names[unsigned(result)]);
}
static void files(){
  fileCount=fileIndex=0;fileMore=false;File dir=SD.open(currentPath);if(!dir||!dir.isDirectory()){notify("Cannot open directory");return;}
  for(unsigned skip=0;skip<filePage*32;++skip){File entry=dir.openNextFile();if(!entry)break;entry.close();}
  while(fileCount<32){File entry=dir.openNextFile();if(!entry)break;
    fileNames[fileCount]=entry.name();int slash=fileNames[fileCount].lastIndexOf('/');if(slash>=0)fileNames[fileCount]=fileNames[fileCount].substring(slash+1);
    filePaths[fileCount]=currentPath+(currentPath.endsWith("/")?"":"/")+fileNames[fileCount];fileDirs[fileCount]=entry.isDirectory();++fileCount;entry.close();
  }if(fileCount==32){File next=dir.openNextFile();fileMore=bool(next);next.close();}dir.close();
}
static bool startLog(){
  if(!mp24::cardReady)return false;
  if(!SD.exists("/hackerphone"))SD.mkdir("/hackerphone");
  for(unsigned n=1;n<100000;++n){snprintf(sessionPath,sizeof(sessionPath),"/hackerphone/recon-%05u.csv",n);if(!SD.exists(sessionPath))break;if(n==99999)return false;}
  session=SD.open(sessionPath,FILE_WRITE);if(!session)return false;
  session.println("uptime_ms,radio,address,address_type,channel,rssi_dbm,family,confidence,name,evidence");session.flush();logging=true;return true;
}
static void saveTargets(uint32_t now){
  if(!logging||uint32_t(now-lastSave)<5000||phone.state().phonePriority())return;lastSave=now;
  // Do not scan the FAT free-space map in the foreground. Short writes below
  // stop recording without making keypad and modem servicing wait on that scan.
  for(auto& t:recon.targets)if(t.fresh(now)){
    char name[160],evidence[256],line[640];csvField(name,sizeof(name),t.name);csvField(evidence,sizeof(evidence),t.match.evidence);
    snprintf(line,sizeof(line),"%lu,%s,%02X:%02X:%02X:%02X:%02X:%02X,%u,%u,%d,\"%s\",%u,%s,%s\n",static_cast<unsigned long>(now),t.ble?"BLE":"WiFi",t.mac[0],t.mac[1],t.mac[2],t.mac[3],t.mac[4],t.mac[5],t.addressType,t.channel,t.signal.value(),dragon::familyName(t.match.family),unsigned(t.match.confidence),name,evidence);
    if(session.print(line)!=strlen(line)){logging=false;session.close();notify("SD write failed");break;}
  }
  if(logging)session.flush();
}
static void startRecon(){if(startListening()){screen=Screen::Recon;recon.pinned=-1;targetFilter=0;if(!logging)startLog();}else notify("Radio initialization failed");}
static void listTargets(){if(!targetVisible(recon.targets[targetIndex]))nextTarget(1);else cacheVendor();}
static void browse(unsigned filter){targetFilter=filter;recon.selected=-1;listTargets();recon.pinned=targetVisible(recon.targets[targetIndex])?targetIndex:-1;screen=Screen::Targets;}
static void chooseScanner(unsigned i){if(i>=unsigned(ScanProfile::Count))return;stopTools();radioMode=RadioMode(scanDefinitions[i].radio);if(startListening())browse(i);else notify("Radio unavailable");}
static uint16_t wifiRates[48]{},bleRates[48]{};
static uint32_t ratesAt=0,previousWifi=0,previousBle=0;static unsigned rateHead=0;
static void sampleRates(uint32_t now){uint32_t elapsed=now-ratesAt;if(elapsed<1000)return;ratesAt=now;
  wifiRates[rateHead]=std::min<uint32_t>(65535,uint64_t(recon.wifiFrames-previousWifi)*1000/elapsed);
  bleRates[rateHead]=std::min<uint32_t>(65535,uint64_t(recon.bleFrames-previousBle)*1000/elapsed);
  previousWifi=recon.wifiFrames;previousBle=recon.bleFrames;rateHead=(rateHead+1)%48;}
static void menuRows(const String* items,unsigned count){int first=menu/6*6;for(unsigned row=0;row<6&&first+row<int(count);++row){int i=first+row;if(i==menu)mp24::frame.fillRoundRect(2,33+row*13,156,12,2,theme().selection);text(5,35+row*13,items[i].substring(0,25));}}
static void moveMenu(mp24::Key k,unsigned count){if(k==mp24::Down)menu=(menu+1)%count;if(k==mp24::Up)menu=(menu+count-1)%count;}
static const char* confidence(dragon::Confidence c){switch(c){case dragon::Confidence::Strong:return "Strong";case dragon::Confidence::Medium:return "Medium";case dragon::Confidence::Low:return "Low";default:return "No signature";}}
static String macText(const uint8_t* mac){char b[18];snprintf(b,sizeof(b),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);return b;}
static bool saveGatt(){
  if(!mp24::cardReady)return false;if(!SD.exists("/hackerphone"))SD.mkdir("/hackerphone");
  char path[64];snprintf(path,sizeof(path),"/hackerphone/gatt-%08lx.csv",(unsigned long)millis());if(SD.exists(path))return false;
  File out=SD.open(path,FILE_WRITE);if(!out)return false;bool ok=out.println("handle,service_uuid,characteristic_uuid,properties")>0;
  GattRow row;for(unsigned i=0;gattRow(i,row);++i){char line[128];int n=snprintf(line,sizeof(line),"%u,%s,%s,%u\n",row.handle,row.service,row.uuid,row.flags);if(out.write((const uint8_t*)line,n)!=size_t(n)){ok=false;break;}}out.flush();out.close();return ok;
}
static void digitInput(uint8_t digit,uint32_t now){
  if(screen==Screen::NetEdit){
    if(networkField==2){if(networkEdit.length()<15)networkEdit+=char('0'+digit);return;}
    static const char* keys[]={" 0",".,?!1","abc2","def3","ghi4","jkl5","mno6","pqrs7","tuv8","wxyz9"};
    bool repeat=previousDigit==digit&&uint32_t(now-tapAt)<800&&networkEdit.length();
    if(repeat){tapIndex=(tapIndex+1)%strlen(keys[digit]);networkEdit.remove(networkEdit.length()-1);}else{if(networkEdit.length()>=(networkField==0?32:63))return;tapIndex=0;}
    char ch=keys[digit][tapIndex];networkEdit+=networkUpper?char(toupper(ch)):ch;previousDigit=digit;tapAt=now;return;
  }
  if(screen==Screen::Home){screen=Screen::Dial;dialNumber="";}
  if(screen==Screen::Dial){if(dialNumber.length()<21)dialNumber+=char('0'+digit);return;}
  if(screen==Screen::Messages){if(recipient.length()<21)recipient+=char('0'+digit);return;}
  if(screen!=Screen::Compose)return;
  static const char* keys[]={" 0",".,?!1","abc2","def3","ghi4","jkl5","mno6","pqrs7","tuv8","wxyz9"};
  bool repeat=previousDigit==digit&&uint32_t(now-tapAt)<800&&message.length()>0;
  if(repeat){tapIndex=(tapIndex+1)%strlen(keys[digit]);message.remove(message.length()-1);}
  else{if(message.length()>=70){notify("70-character SMS limit");return;}tapIndex=0;}
  message+=keys[digit][tapIndex];previousDigit=digit;tapAt=now;
}
static void key(mp24::Key k,uint32_t now){
  if(k==mp24::None)return;
  if(shuttingDown)return;
  if(k==mp24::VolumeUp||k==mp24::VolumeDown){musicVolume(int(musicStatus().volume)+(k==mp24::VolumeUp?5:-5));volumeAt=volumeSaveAt=now;return;}
  if(locked)return;

  if(attackStatus.active&&screen!=Screen::AttackRun)attackStop();
  if(unsigned(k)<=unsigned(mp24::K9)){digitInput(unsigned(k),now);return;}
  if(k==mp24::C){missionStop();irStop();trackerLiveStop();attackStop();labStop();networkStop();gattStop();screen=Screen::Home;menu=0;recon.selected=recon.pinned=-1;return;}
  bool accept=k==mp24::A||k==mp24::Enter;
  if(screen==Screen::Home){if(k==mp24::Down||k==mp24::Right)menu=(menu+1)%8;if(k==mp24::Up||k==mp24::Left)menu=(menu+7)%8;if(accept){switch(menu){case 0:screen=Screen::Dial;break;case 1:screen=Screen::Messages;break;case 2:screen=Screen::Tools;menu=0;break;case 3:openInfrared();break;case 4:screen=Screen::Files;irBrowsing=false;if(mp24::cardReady)files();break;case 5:screen=Screen::Settings;menu=0;break;case 6:screen=Screen::Diagnostics;break;case 7:screen=Screen::Music;break;}}return;}
  if(screen==Screen::Incoming){if(accept)notify("Voice audio port pending");if(k==mp24::B)showResult(phone.hangup(now));return;}
  if(screen==Screen::Dial){
    if(k==mp24::Star&&dialNumber.isEmpty())dialNumber="+";
    if(k==mp24::B){if(phone.state().call!=Call::Idle)showResult(phone.hangup(now));else if(dialNumber.length())dialNumber.remove(dialNumber.length()-1);else screen=Screen::Home;}
    if(accept){if(phone.state().sim!=Sim::Ready)showResult(Request::NoSim);else notify("Voice audio port pending");}return;
  }
  if(screen==Screen::Messages){if(k==mp24::Star&&recipient.isEmpty())recipient="+";if(k==mp24::B){if(recipient.length())recipient.remove(recipient.length()-1);else screen=Screen::Home;}if(accept){if(validNumber(recipient.c_str())){screen=Screen::Compose;previousDigit=255;}else notify("Enter a phone number");}return;}
  if(screen==Screen::Compose){if(k==mp24::B){if(message.length())message.remove(message.length()-1);previousDigit=255;}if(accept){screen=Screen::ConfirmSms;previousDigit=255;smsSubmitted=false;}return;}
  if(screen==Screen::ConfirmSms){
    if(k==mp24::B){if(phone.state().smsOutcome==Outcome::Pending)notify("Sending: please wait");else screen=Screen::Compose;}
    if(accept){if(smsSubmitted)notify("Already submitted; B edits");else{auto r=phone.sendSms(recipient.c_str(),message.c_str(),now);smsSubmitted=r==Request::Accepted;showResult(r);}}return;
  }
  if(screen==Screen::Recon){if(accept){browse(0);}if(k==mp24::D){if(radioRunning)radioStop();else startListening();}if(k==mp24::B){screen=Screen::Tools;menu=0;}return;}
  if(screen==Screen::Tools){
    moveMenu(k,20);
    if(accept){switch(menu){
      case 0:startRecon();break;
      case 1:case 2:case 3:radioSetMode(RadioMode::Wifi);if(startListening())browse(menu);else notify("Radio busy; retry shortly");break;
      case 4:radioSetMode(RadioMode::Ble);if(startListening())browse(4);else notify("Radio busy; retry shortly");break;
      case 5:radioSetMode(RadioMode::Wifi);if(startListening())screen=Screen::Channels;break;
      case 6:screen=Screen::Alerts;eventIndex=0;break;
      case 7:case 8:if(storageStatus.recording){captureStop();notify("Capture saved");}else if(startListening()&&captureStart(menu==8))notify(menu==8?"Recording EAPOL":"Recording raw WiFi");else notify("Capture needs working SD");break;
      case 9:screen=Screen::Attacks;menu=0;break;
      case 10:radioSetMode(RadioMode((unsigned(radioMode)+1)%3));break;
      case 11:stopTools();notify("All radio tools stopped");break;
      case 12:screen=Screen::Scanners;menu=0;break;
      case 13:if(startListening())screen=Screen::PacketMonitor;break;
      case 14:stopTools();screen=Screen::Network;menu=0;break;
      case 15:screen=Screen::SavedLists;menu=0;break;
      case 16:screen=Screen::TrackerLiveConfirm;break;case 17:screen=Screen::Mission;menu=0;break;case 18:screen=Screen::Playlist;menu=0;break;case 19:screen=Screen::MacSettings;menu=0;break;
    }}if(k==mp24::B){screen=Screen::Home;menu=0;}return;
  }
  if(screen==Screen::SavedLists){moveMenu(k,6);if(accept){if(menu<3)notify(saveReconList(menu==0?0:menu==1?1:4)?savedListMessage:"SD save failed");
    else if(menu==3){if(saveSsidList()){snprintf(attackOptions.ssidPath,sizeof(attackOptions.ssidPath),"%s",savedListPath);notify("SSID playlist saved & selected");}else notify("SSID save failed");}
    else if(menu==4){stopTools();irBrowsing=false;currentPath="/hackerphone/lists";filePage=0;files();screen=Screen::Files;}
    else{stopTools();for(auto& t:recon.targets)t={};notify("Observation list cleared");}}
    if(k==mp24::B){screen=Screen::Tools;menu=15;}return;}
  if(screen==Screen::Scanners){moveMenu(k,unsigned(ScanProfile::Count));if(accept)chooseScanner(menu);if(k==mp24::B){screen=Screen::Tools;menu=12;}return;}
  if(screen==Screen::PacketMonitor){if(k==mp24::D)handshakePage=!handshakePage;if(k==mp24::Left||k==mp24::Right)radioSetMode(RadioMode((unsigned(radioMode)+1)%3));if(k==mp24::B){screen=Screen::Tools;menu=13;}return;}
  if(screen==Screen::Network){moveMenu(k,11+unsigned(NetworkProbe::Count));if(accept){
    if(menu<=2){networkField=menu;networkEdit=menu==0?networkSsid:menu==1?networkPassword:networkHost;previousDigit=255;networkShowPassword=false;screen=Screen::NetEdit;}
    else if(menu==3)notify(networkJoin(networkSsid.c_str(),networkPassword.c_str())?"Joining network":"Join unavailable; stop tool");
    else if(menu==4){preferences.putString("netssid",networkSsid);preferences.putString("netpass",networkPassword);notify("WiFi profile saved locally");}
    else if(menu==5){networkSsid=preferences.getString("netssid","HackerPhone-Lab");networkPassword=preferences.getString("netpass","");notify("Saved profile loaded");}
    else if(menu==6){networkHost=ipText(networkStatus().gateway);notify("Target set to gateway");}
    else if(menu==9){screen=Screen::NetResults;networkResult=0;}
    else if(menu==10){networkStop();notify("Network tools stopped");}
    else{networkSelection=menu;screen=Screen::NetReview;}
  }if(k==mp24::B){screen=Screen::Tools;menu=14;}return;}
  if(screen==Screen::NetEdit){
    if(k==mp24::B){if(networkEdit.length())networkEdit.remove(networkEdit.length()-1);previousDigit=255;}
    if(k==mp24::Star){if(networkField==2){if(networkEdit.length()<15)networkEdit+='.';}else networkUpper=!networkUpper;previousDigit=255;}
    if(k==mp24::Hash&&networkEdit.length()<(networkField==0?32:networkField==1?63:15)){networkEdit+=' ';previousDigit=255;}
    if((k==mp24::Up||k==mp24::Down)&&networkEdit.length()){unsigned i=networkEdit.length()-1;char c=networkEdit[i];c=k==mp24::Up?(c>=126?32:c+1):(c<=32?126:c-1);networkEdit.setCharAt(i,c);previousDigit=255;}
    if(k==mp24::D)networkShowPassword=!networkShowPassword;
    if(accept){uint32_t ip;uint8_t mac[6];bool ok=true;
      if(networkField==2&&!parseIpv4(networkEdit.c_str(),ip)){notify("Enter a valid IPv4 address");ok=false;}
      if((networkField==3||networkField==4)&&!parseMac(networkEdit.c_str(),mac)){notify("Enter 12 hex digits / MAC");ok=false;}
      if(networkField==5&&(networkEdit.isEmpty()||networkEdit.length()>32)){notify("SSID must be 1-32 bytes");ok=false;}
      if(ok){if(networkField==0)networkSsid=networkEdit;else if(networkField==1)networkPassword=networkEdit;else if(networkField==2)networkHost=networkEdit;
        else if(networkField==3){memcpy(attackOptions.target.bssid,mac,6);attackOptions.target.hasAp=true;attackOptions.target.hasClient=false;}
        else if(networkField==4){memcpy(attackOptions.target.client,mac,6);attackOptions.target.hasClient=true;}
        else if(networkField==7){String destination=currentPath+(currentPath=="/"?"":"/")+networkEdit;notify(fileRename(filePaths[fileIndex].c_str(),destination.c_str())?"Renamed":"Rename unavailable");}else if(networkField==8){String path=currentPath+(currentPath=="/"?"":"/")+networkEdit;notify(safeSdPath(path.c_str())&&path.startsWith("/hackerphone/")&&!SD.exists(path)&&SD.mkdir(path)?"Folder created":"Invalid folder");}else if(networkField==6){if(!ssidAdd(networkEdit.c_str()))notify("Invalid / duplicate / full");}else snprintf(attackOptions.target.ssid,33,"%s",networkEdit.c_str());networkEdit="";if(networkField>=7)files();screen=networkField>=7?Screen::Files:networkField==6?Screen::Playlist:networkField<3?Screen::Network:Screen::AttackConfig;menu=networkField<3?networkField:0;}}return;
  }
  if(screen==Screen::NetReview){if(k==mp24::B){screen=Screen::Network;menu=networkSelection;}if(accept){bool ok=false;if(!phone.state().phonePriority())ok=networkSelection==7||networkSelection==8?networkHotspot(networkSsid.c_str(),networkPassword.c_str(),networkSelection==8):networkProbe(NetworkProbe(networkSelection-11),networkHost.c_str());
      if(ok){screen=Screen::NetResults;networkResult=0;}else notify("Needs connection / idle tool");}return;}
  if(screen==Screen::NetResults){auto s=networkStatus();if(s.rows){if(k==mp24::Down||k==mp24::Right)networkResult=(networkResult+1)%s.rows;if(k==mp24::Up||k==mp24::Left)networkResult=(networkResult+s.rows-1)%s.rows;}
    if(k==mp24::D)networkCancelProbe();if(k==mp24::Hash)notify(networkSaveResults()?"Results saved to SD":"Stop scan / check SD");if(k==mp24::B){screen=Screen::Network;menu=9;}return;}
  if(screen==Screen::Targets){
    if(k==mp24::Down||k==mp24::Right)nextTarget(1);if(k==mp24::Up||k==mp24::Left)nextTarget(-1);
    if(k==mp24::Hash&&targetVisible(recon.targets[targetIndex])){recon.targets[targetIndex].marked=!recon.targets[targetIndex].marked;notify(recon.targets[targetIndex].marked?"Marked / retained":"Unmarked");}
    if(targetVisible(recon.targets[targetIndex])){if(accept){recon.selected=targetIndex;screen=Screen::Hunt;}if(k==mp24::D){screen=Screen::TargetTools;menu=0;}}
    if(k==mp24::B){screen=Screen::Tools;menu=0;recon.pinned=-1;}return;
  }
  if(screen==Screen::TargetTools){
    moveMenu(k,13);auto& t=recon.targets[targetIndex];
    if(accept){switch(menu){case 0:recon.selected=targetIndex;screen=Screen::Hunt;break;
      case 1:trackerLiveStop();attackStop();labStop();if(t.ble&&t.fresh(now)&&gattConnect(t.mac,t.addressType)){screen=Screen::Gatt;gattIndex=0;}else notify("Choose a live BLE device");break;
      case 2:if(t.ble&&validLegacyAdvert(t.advert,t.advertSize)){pendingLab=LabMode::BleReplay;screen=Screen::LabConfirm;}else notify("Needs complete <=31B advert");break;
      case 3:detailPage=0;screen=Screen::TargetInfo;break;case 4:detailPage=1;screen=Screen::TargetInfo;break;
      case 5:if(attackSelectAp(targetIndex)){screen=Screen::Attacks;menu=0;notify("AP copied to attack target");}else notify("Choose a live WiFi AP");break;
      case 6:if(attackSelectClient(targetIndex)){screen=Screen::Attacks;menu=0;notify("Client target selected");}else notify("Needs client of chosen AP");break;
      case 7:if(t.ble&&t.fresh(now)){screen=Screen::TrackerConfirm;}else notify("Select a live BLE tracker");break;
      case 8:if(!t.ble&&t.accessPoint&&*t.name){networkSsid=t.name;stopTools();screen=Screen::Network;menu=1;}else notify("Select an AP with SSID");break;
      case 9:if(t.ble&&t.addressType==1&&t.fresh(now)&&validLegacyAdvert(t.advert,t.advertSize)){pendingLab=LabMode::BleIdentityReplay;screen=Screen::LabConfirm;}else notify("Needs live random BLE MAC");break;
      case 10:if(!t.ble&&t.probe&&*t.name){networkSsid=t.name;networkPassword="";stopTools();networkSelection=8;screen=Screen::NetReview;}else notify("Select a named WiFi probe");break;
      case 11:case 12:if(!t.ble&&t.accessPoint&&t.fresh(now)){captureStop();radioMode=RadioMode::Wifi;channelLock=t.channel;bool ok=startListening()&&captureStart(menu==12,t.mac);notify(ok?"Recording selected AP":"Capture unavailable");}else notify("Select a live AP");break;}}
    if(k==mp24::B)screen=Screen::Targets;return;
  }
  if(screen==Screen::TrackerLiveConfirm){if(k==mp24::B){screen=Screen::Tools;menu=16;}if(accept){if(!phone.state().phonePriority()&&trackerLiveStart(15))screen=Screen::TrackerLiveRun;else notify("Stop other radio tools first");}return;}
  if(screen==Screen::TrackerLiveRun){if(k==mp24::B||accept){trackerLiveStop();screen=Screen::Tools;menu=16;}return;}
  if(screen==Screen::TrackerConfirm){if(k==mp24::B)screen=Screen::TargetTools;if(accept){auto& t=recon.targets[targetIndex];attackStop();labStop();networkStop();if(t.fresh(now)&&!phone.state().phonePriority()&&gattTrackerSound(t.mac,t.addressType)){screen=Screen::Gatt;gattIndex=0;}else notify("Tracker unavailable / busy");}return;}
  if(screen==Screen::TargetInfo){if(k==mp24::Right)detailPage=(detailPage+1)%5;if(k==mp24::Left)detailPage=(detailPage+4)%5;if(k==mp24::B)screen=Screen::TargetTools;return;}
  if(screen==Screen::Hunt){if(k==mp24::B){recon.selected=-1;screen=Screen::Targets;}return;}
  if(screen==Screen::Channels){if(k==mp24::Left)channelLock=channelLock?channelLock-1:11;if(k==mp24::Right)channelLock=(channelLock+1)%12;if(k==mp24::B){screen=Screen::Tools;menu=5;}return;}
  if(screen==Screen::Alerts){unsigned count=std::min<uint32_t>(16,recon.eventCount);if(count&&(k==mp24::Down||k==mp24::Right))eventIndex=(eventIndex+1)%count;if(count&&(k==mp24::Up||k==mp24::Left))eventIndex=(eventIndex+count-1)%count;if(k==mp24::B){screen=Screen::Tools;menu=6;}return;}
  if(screen==Screen::Gatt){
    auto g=gattStatus();if(g.rows&&(k==mp24::Down||k==mp24::Right))gattIndex=(gattIndex+1)%g.rows;if(g.rows&&(k==mp24::Up||k==mp24::Left))gattIndex=(gattIndex+g.rows-1)%g.rows;
    GattRow row;if(gattRow(gattIndex,row)&&!g.busy){if(accept&&!gattRead(row.handle))notify("GATT queue busy");if(k==mp24::D&&!gattSubscribe(row.handle,!(row.flags&32)))notify("GATT queue busy");}
    if(k==mp24::Hash)notify(saveGatt()?"GATT tree saved to SD":"SD save failed");
    if(k==mp24::B){gattStop();screen=Screen::TargetTools;}return;
  }
  if(screen==Screen::Attacks){
    constexpr unsigned modes=unsigned(AttackMode::Count);moveMenu(k,modes+3);
    if(accept){if(unsigned(menu)<modes){pendingAttack=AttackMode(menu);screen=Screen::AttackConfirm;}else if(unsigned(menu)==modes){screen=Screen::AttackConfig;menu=0;}else if(unsigned(menu)==modes+1){screen=Screen::Lab;menu=0;}else{attackStop();labStop();notify("Transmission stopped");}}
    if(k==mp24::B){attackStop();screen=Screen::Tools;menu=9;}return;
  }
  if(screen==Screen::AttackConfig){moveMenu(k,12);if(accept){auto& t=attackOptions.target;switch(menu){
    case 0:case 1:case 2:networkField=menu+3;networkEdit=menu==0?macText(t.bssid):menu==1?macText(t.client):String(t.ssid);previousDigit=255;screen=Screen::NetEdit;break;
    case 3:t.channel=t.channel%11+1;break;case 4:t.destinationChannel=t.destinationChannel%11+1;break;
    case 5:t.hasClient=false;memset(t.client,0,6);break;case 6:notify(attackListAdd()?"Target copied into list":"Invalid target / list full");break;
    case 7:attackOptions.useList=!attackOptions.useList;break;case 8:attackListClear();notify("Attack list cleared");break;case 9:notify((String("Added clients: ")+attackListObservedClients()).c_str());break;case 10:notify(attackListCount()&&attackListRemove(attackListCount()-1)?"Removed last target":"List is empty");break;case 11:notify((String("Marked targets added: ")+markedAttackTargets()).c_str());break;
  }}if(k==mp24::B){screen=Screen::Attacks;menu=unsigned(AttackMode::Count);}return;}
  if(screen==Screen::AttackConfirm){
    if(k==mp24::Hash)attackOptions.useList=!attackOptions.useList;
    if(k==mp24::Left)attackOptions.durationSeconds=attackOptions.durationSeconds<=5?60:attackOptions.durationSeconds-5;
    if(k==mp24::Right)attackOptions.durationSeconds=attackOptions.durationSeconds>=60?5:attackOptions.durationSeconds+5;
    if(k==mp24::D){auto& rate=attackOptions.packetsPerSecond;rate=rate<10?10:rate<25?25:rate<50?50:rate<100?100:1;}
    if(accept){if(phone.state().phonePriority())notify("Phone session has priority");else if(attackStart(pendingAttack))screen=Screen::AttackRun;else notify(attackStatus.message);}
    if(k==mp24::B){screen=Screen::Attacks;menu=unsigned(pendingAttack);}return;
  }
  if(screen==Screen::AttackRun){if(k==mp24::B||accept){attackStop();screen=Screen::Attacks;menu=unsigned(pendingAttack);}return;}
  if(screen==Screen::Lab){moveMenu(k,3);if(accept){if(menu==2)labStop();else{pendingLab=menu==0?LabMode::WifiBeacon:LabMode::BleBeacon;screen=Screen::LabConfirm;}}if(k==mp24::B){labStop();screen=Screen::Attacks;menu=0;}return;}
  if(screen==Screen::LabConfirm){if(accept){attackStop();auto& t=recon.targets[targetIndex];bool ok=!phone.state().phonePriority()&&labStart(pendingLab,channelLock?channelLock:1,t.advert,t.advertSize,t.mac,t.addressType);screen=Screen::Lab;menu=2;notify(ok?"Transmission started":"Could not start test");}if(k==mp24::B)screen=Screen::Lab;return;}
  if(screen==Screen::Infrared){
    moveMenu(k,6);
    if(accept){switch(menu){
      case 0:if(!phone.state().phonePriority()&&irLearn())screen=Screen::IrSignal;else notify(irStatus.message);break;
      case 1:if(irDirectories()){irBrowsing=true;currentPath="/hackerphone/ir";filePage=0;files();screen=Screen::Files;}else notify(irStatus.message);break;
      case 2:screen=Screen::IrSignal;break;
      case 3:{uint32_t hz=irFrequency();irSetFrequency(hz==36000?38000:hz==38000?40000:hz==40000?56000:36000);break;}
      case 4:notify(irSave()?"Signal saved on SD":irStatus.message);break;
      case 5:irStop();break;
    }}if(k==mp24::B){irStop();screen=Screen::Home;menu=3;}return;
  }
  if(screen==Screen::IrSignal){
    if(k==mp24::B){irStop();screen=Screen::Infrared;menu=0;return;}
    if(irBusy()){if(accept)irStop();return;}
    if(accept&&!phone.state().phonePriority())irSend();if(k==mp24::D)irSave();
    if(k==mp24::Right)irNext();if(k==mp24::Left)irPrevious();return;
  }
  if(screen==Screen::Tones){moveMenu(k,11);if(accept||k==mp24::Left||k==mp24::Right){if(menu<4){if(accept)tonePreview(menu);else {toneEnabled^=1u<<menu;tonesSave();}}
    else if(menu==4){toneVolume=std::max(0,std::min(100,int(toneVolume)+(k==mp24::Left?-5:5)));tonesSave();}
    else if(menu<9){String path=String("/hackerphone/tones/")+(menu==5?"call":menu==6?"sms":menu==7?"recon":"tool")+".rtttl";notify(toneLoad(menu-5,path.c_str())?"RTTTL loaded from SD":"Missing or invalid RTTTL");}
    else if(menu==9){for(unsigned i=0;i<4;++i)toneReset(i);notify("Retro defaults restored");}else musicStop();}
    if(k==mp24::B){screen=Screen::Settings;menu=11;}return;}
  if(screen==Screen::FileActions){moveMenu(k,7);if(k==mp24::D&&fileBusy()){fileCancel();notify("File operation cancelled");}if(accept){if(menu==0){networkField=7;networkEdit=fileNames[fileIndex];previousDigit=255;screen=Screen::NetEdit;}
    else if(menu==1){String destination=filePaths[fileIndex]+".copy";notify(fileCopy(filePaths[fileIndex].c_str(),destination.c_str())?"Copy started": "Copy unavailable");}
    else if(menu==2)screen=Screen::TrashConfirm;else if(menu==3){networkField=8;networkEdit="";previousDigit=255;screen=Screen::NetEdit;}
    else if(menu==4)notify(fileBackupSpiffs()?"Backup started (read only)":"Backup unavailable");else {uploadChoice=menu-5;screen=Screen::UploadReview;}}if(k==mp24::B)screen=Screen::Files;return;}
  if(screen==Screen::UploadReview){if(k==mp24::B)screen=Screen::FileActions;if(accept)notify(uploadStart(uploadChoice,filePaths[fileIndex].c_str())?"Upload started":uploadStatus().message);if(k==mp24::D)uploadCancel();return;}
  if(screen==Screen::TrashConfirm){if(k==mp24::B)screen=Screen::FileActions;if(accept){notify(fileTrash(filePaths[fileIndex].c_str())?"Moved to SD trash":"Move unavailable");files();screen=Screen::Files;}return;}
  if(screen==Screen::Playlist){moveMenu(k,7);if(k==mp24::Left&&ssidCount())playlistIndex=(playlistIndex+ssidCount()-1)%ssidCount();if(k==mp24::Right&&ssidCount())playlistIndex=(playlistIndex+1)%ssidCount();if(accept){switch(menu){
    case 0:networkField=6;networkEdit="";previousDigit=255;screen=Screen::NetEdit;break;
    case 1:notify(ssidRemove(playlistIndex)?"SSID removed":"List empty");if(playlistIndex>=ssidCount())playlistIndex=0;break;
    case 2:notify(ssidGenerate(std::min(10u,64-ssidCount()))?"Generated up to 10 SSIDs":"List full");break;
    case 3:notify(ssidSave()?"Saved / beacon list selected":"Save failed");break;
    case 4:notify(ssidLoad(attackOptions.ssidPath)?"Playlist loaded":"Select a text file first");break;
    case 5:for(auto& t:recon.targets)if(t.used&&!t.ble&&(t.accessPoint||t.probe))ssidAdd(t.name);notify("Observed SSIDs added");break;
    case 6:ssidClear();playlistIndex=0;notify("Editor cleared");break;}}
    if(k==mp24::B){screen=Screen::Tools;menu=18;}return;
  }
  if(screen==Screen::Mission){moveMenu(k,6);if(accept){if(menu<3){stopTools();if(logging){session.flush();session.close();logging=false;}notify(missionStart(menu)?"Mission started": "Mission needs idle radio/SD");}
    else if(menu==3){missionStop();notify(mission.message);}else if(menu==4){currentPath="/hackerphone/missions";irBrowsing=musicBrowsing=false;filePage=0;files();screen=Screen::Files;}else notify(targetsJsonSave()?"Marauder JSON saved":"Export failed");}
    if(k==mp24::B){screen=Screen::Tools;menu=17;}return;
  }
  if(screen==Screen::MacSettings){moveMenu(k,6);if(accept){bool ok=false;if(menu<2)ok=radioRandomMac(menu==0);else if(menu<4)ok=radioMac(menu==2,nullptr);else{auto& t=recon.targets[targetIndex];if(t.used&&!t.ble)ok=radioMac(menu==4,t.mac);}notify(ok?"MAC applied until reboot":"Stop network/radio first");}if(k==mp24::B){screen=Screen::Tools;menu=19;}return;}
  if(screen==Screen::Music){
    if(accept){auto m=musicStatus();if(m.busy)musicPause();else if(*m.path)musicPlay(m.path);}if(k==mp24::D){musicStop();nextMusic=0;}
    if(k==mp24::Left||k==mp24::Right){musicStop();nextMusic=k==mp24::Left?-1:1;}
    if(k==mp24::Hash){musicRepeat(!musicStatus().repeat);preferences.putBool("repeat",musicStatus().repeat);}
    if(k==mp24::Up){musicBrowsing=true;irBrowsing=false;currentPath="/hackerphone/music";if(!SD.exists(currentPath))SD.mkdir(currentPath);filePage=0;files();screen=Screen::Files;}
    if(k==mp24::B){screen=Screen::Home;menu=7;}return;
  }
  if(screen==Screen::Leds){moveMenu(k,16);if(accept||k==mp24::Left||k==mp24::Right){
    if(menu==0)ledSettings.pattern=LedPattern((unsigned(ledSettings.pattern)+1)%unsigned(LedPattern::Count));
    else if(menu==1)ledSettings.brightness=uint8_t(std::max(0,std::min(255,int(ledSettings.brightness)+(k==mp24::Left?-16:16))));
    else if(menu==2)ledsTest(now);else if(menu<7)ledSettings.notifications^=1u<<(menu-3);
    else if(menu==7)ledSettings.mask=ledSettings.mask?0:255;else ledSettings.mask^=1u<<(menu-8);
    ledsSave();}if(k==mp24::B){screen=Screen::Settings;menu=8;}return;
  }
  if(screen==Screen::Files){
    if(k==mp24::Hash&&fileCount){if(logging){session.flush();session.close();logging=false;}screen=Screen::FileActions;menu=0;return;}
    if(k==mp24::D&&fileCount&&!fileDirs[fileIndex]&&fileNames[fileIndex].endsWith(".txt")&&filePaths[fileIndex].startsWith("/hackerphone/")&&filePaths[fileIndex].length()<sizeof(attackOptions.ssidPath)){
      snprintf(attackOptions.ssidPath,sizeof(attackOptions.ssidPath),"%s",filePaths[fileIndex].c_str());notify("SSID playlist selected");return;}
    if(k==mp24::Down&&fileCount)fileIndex=(fileIndex+1)%fileCount;if(k==mp24::Up&&fileCount)fileIndex=(fileIndex+fileCount-1)%fileCount;
    if(k==mp24::Left&&filePage){--filePage;files();}if(k==mp24::Right&&fileMore){++filePage;files();}
    if(k==mp24::B){if(musicBrowsing&&currentPath=="/hackerphone/music"){screen=Screen::Music;musicBrowsing=false;}else if(irBrowsing&&currentPath=="/hackerphone/ir"){screen=Screen::Infrared;menu=1;}else if(currentPath=="/"){screen=Screen::Home;menu=0;}else{int slash=currentPath.lastIndexOf('/');currentPath=slash<=0?"/":currentPath.substring(0,slash);filePage=0;files();}}
    if(accept&&fileCount){if(fileDirs[fileIndex]){currentPath=filePaths[fileIndex];filePage=0;files();}else if(fileNames[fileIndex].endsWith(".mp3")||fileNames[fileIndex].endsWith(".MP3")){if(musicPlay(filePaths[fileIndex].c_str())){screen=Screen::Music;musicBrowsing=false;}else notify("Stop current track first");}else if(fileNames[fileIndex].endsWith(".json")){stopTools();if(targetsJsonLoad(filePaths[fileIndex].c_str()))browse(0);else notify("Not a valid AP list JSON");}else if(fileNames[fileIndex].endsWith(".hpl")){stopTools();if(loadReconList(filePaths[fileIndex].c_str()))browse(0);notify(savedListMessage);}else if(irBrowsing&&fileNames[fileIndex].endsWith(".ir")){irOpen(filePaths[fileIndex].c_str());screen=Screen::IrSignal;}else{File file=SD.open(filePaths[fileIndex]);filePreview="";while(file&&file.available()&&filePreview.length()<168){char c=file.read();filePreview+=c>=32&&c<127?c:' ';}file.close();screen=Screen::FileView;}}return;
  }
  if(screen==Screen::FileView){if(k==mp24::B)screen=Screen::Files;return;}
  if(screen==Screen::Settings){
    moveMenu(k,12);
    if(accept){switch(menu){case 0:setTheme(themeIndex+1);break;case 1:if(radioRunning)radioStop();else startListening();break;
      case 2:radioSetMode(RadioMode((unsigned(radioMode)+1)%3));break;case 3:channelLock=(channelLock+1)%12;break;
      case 4:if(logging){session.flush();session.close();logging=false;}else if(!startLog())notify("SD log unavailable");break;case 5:phone.begin(now);break;case 6:radioSetBleActive(!bleActiveScan);preferences.putBool("bleactive",bleActiveScan);break;case 7:screen=Screen::Music;break;case 8:screen=Screen::Leds;menu=0;break;case 9:notify(musicTone()?"Audio test playing":"Stop current track first");break;case 10:locked=true;mp24::backlight(false);break;case 11:screen=Screen::Tones;menu=0;break;}}
    if(k==mp24::B){screen=Screen::Home;menu=0;}return;
  }
  if(k==mp24::B)screen=Screen::Home;
}
static void render(uint32_t now){
  auto& f=mp24::frame;
  if(shuttingDown){header("POWER OFF");text(12,49,"Saving and stopping...");text(12,68,"Release red button",theme().accent);text(12,90,"USB may keep power on",theme().muted);mp24::present();return;}
  if(locked){f.fillSprite(TFT_BLACK);text(56,42,"LOCKED",theme().accent);text(14,65,"Tap red button to wake");if(volumeAt&&uint32_t(now-volumeAt)<1500)text(45,86,String("Volume ")+musicStatus().volume+"%");mp24::present();return;}
  if(screen==Screen::Home){
    header("HACKERPHONE");f.setSwapBytes(true);f.pushImage(79,30,vtsWidth,vtsHeight,vtsPixels);
    static const char* names[]={"Phone","Messages","Security","Infrared","SD files","Settings","Hardware","Music"};
    for(int row=0,first=menu/6*6;row<6&&first+row<8;++row){int i=first+row;if(i==menu)f.fillRoundRect(2,33+row*13,75,12,2,theme().selection);else f.fillRect(2,33+row*13,75,12,theme().background);text(5,35+row*13,names[i]);}
    footer("A:Open  C:Home");
  }else if(screen==Screen::Dial){header("PHONE");text(5,35,"Number (* for +)",theme().muted);wrap(5,49,dialNumber,24,2);text(5,82,"Voice audio port pending",TFT_ORANGE);text(5,97,"No calls sent by USB tests",TFT_DARKGREY);footer("A:Call  B:Erase  C:Home");}
  else if(screen==Screen::Messages){header("NEW MESSAGE");text(5,35,"To (* for +)",theme().muted);wrap(5,49,recipient,24,2);text(5,87,"Write without a SIM.",theme().muted);text(5,99,"Sending needs service.",theme().muted);footer("A:Write  B:Erase");}
  else if(screen==Screen::Compose){header("COMPOSE / MULTI-TAP");wrap(5,35,message);text(122,104,String(message.length())+"/70",theme().muted);footer("A:Review  B:Erase");}
  else if(screen==Screen::ConfirmSms){header("REVIEW SMS");text(5,34,recipient);wrap(5,48,message,24,4);
    const auto outcome=phone.state().smsOutcome;text(5,96,outcome==Outcome::Accepted?"Accepted by modem":outcome==Outcome::Uncertain?"Status unknown - no retry":outcome==Outcome::Pending?"Sending...":"A sends this message",TFT_ORANGE);footer("A:Send  B:Edit");}
  else if(screen==Screen::Recon){
    header("RECON / RSSI RADAR");f.fillRect(0,30,160,86,theme().radar);
    for(int r:{14,28,40})f.drawCircle(80,73,r,theme().grid);f.drawFastHLine(38,73,85,theme().grid);f.drawFastVLine(80,32,82,theme().grid);
    float sweep=float(now%4000)*6.2831853f/4000;
    for(unsigned trail=1;trail<5;++trail){float a=sweep-trail*.09f;f.drawLine(80,73,80+40*cosf(a),73+40*sinf(a),theme().grid);}
    f.drawLine(80,73,80+40*cosf(sweep),73+40*sinf(sweep),theme().sweep);
    unsigned plotted=0;for(auto& t:recon.targets)if(t.fresh(now)&&t.match.confidence!=dragon::Confidence::None&&plotted<5){
      uint32_t hash=2166136261u;for(uint8_t c:t.mac)hash=(hash^c)*16777619u;
      float angle=(hash%360)*.0174533f,r=8+std::clamp((-35.f-t.signal.smooth)/65.f,0.f,1.f)*30;
      int x=80+r*cosf(angle),y=73+r*sinf(angle);uint16_t col=color(dragon::familyColor(t.match.family));
      int labelX=x<80?2:116,labelY=34+int(plotted)*15;f.drawLine(x,y,labelX+(x<80?37:0),labelY+4,col);f.fillCircle(x,y,3,col);if(uint32_t(now-t.last)<1000)f.drawCircle(x,y,4+(now/180)%3,col);
      text(labelX,labelY,String(dragon::familyName(t.match.family)).substring(0,7),col);++plotted;
    }
    text(3,31,radioRunning?(radioBle?"BLE RX":"WIFI RX"):"PAUSED",TFT_GREEN);text(112,106,String(recon.count(now,true))+" clues",TFT_YELLOW);footer("A:List D:Pause C:Home");
  }else if(screen==Screen::Targets||screen==Screen::Hunt){
    header(screen==Screen::Hunt?"FOX HUNT / SIGNAL":scanDefinitions[targetFilter].name);auto& t=recon.targets[targetIndex];
    if(!targetVisible(t))text(5,40,"No matching observations");else{
      text(5,33,String(dragon::familyName(t.match.family)).substring(0,25),color(dragon::familyColor(t.match.family)));
      text(5,45,String(*t.name?t.name:"(unnamed)").substring(0,25));
      text(5,57,String(t.signal.value())+"dBm "+(t.fresh(now)?"LIVE":"STALE")+" CH"+String(t.channel),theme().accent);
      if(screen==Screen::Hunt){
        int previous=-1;for(unsigned i=0;i<48;++i){int v=t.history.recent(i,now);if(v==dragon::SignalHistory::Gap){previous=-1;continue;}int y=94-std::clamp(v+100,0,70)*26/70;if(previous>=0)f.drawLine(6+(i-1)*3,previous,6+i*3,y,theme().sweep);previous=y;}
        text(5,99,String(t.peak)+" peak / "+t.signal.direction(),theme().muted);
      }else{text(5,70,macText(t.mac),theme().muted);text(5,82,String(*vendorName?vendorName:addressKind(t.mac,t.ble,t.addressType)).substring(0,25),theme().muted);text(5,95,String(confidence(t.match.confidence))+" / "+t.packets+" frames",theme().muted);}
    }footer(screen==Screen::Hunt?"Signal only  B:Back":"Up/Dn A:Hunt D:More");
  }else if(screen==Screen::Files){header("SD FILE BROWSER");text(5,34,mp24::cardReady?currentPath.substring(0,19)+" p"+String(filePage+1):"SD unavailable");int first=(fileIndex/6)*6;for(int row=0;row<6&&first+row<fileCount;++row){int i=first+row;if(i==fileIndex)f.fillRect(1,47+row*11,158,11,theme().selection);text(4,48+row*11,String(fileDirs[i]?"+ ":"  ")+fileNames[i].substring(0,23));}footer("A:Open #:Tools L/R:Page");}
  else if(screen==Screen::FileView){header("FILE PREVIEW");wrap(5,35,filePreview);footer("First 168 bytes / B:Back");}
  else if(screen==Screen::Tones){header("RINGTONES / RTTTL");String items[]={String("Call tone: ")+((toneEnabled&1)?"on":"off"),String("Text tone: ")+((toneEnabled&2)?"on":"off"),String("Recon alert: ")+((toneEnabled&4)?"on":"off"),String("Tool saved: ")+((toneEnabled&8)?"on":"off"),String("Alert volume: ")+toneVolume+"%","Load call.rtttl from SD","Load sms.rtttl from SD","Load recon.rtttl from SD","Load tool.rtttl from SD","Reset retro tones","Stop preview"};menuRows(items,11);footer("A:Preview L/R:Toggle/vol");}
  else if(screen==Screen::FileActions){header("SD FILE ACTIONS");String items[]={"Rename selected item","Copy selected file","Move file to trash","Create folder here","Back up original SPIFFS","Upload CSV to WiGLE","Upload CSV to WDG Wars"};menuRows(items,7);footer(fileBusy()?"D:Cancel file operation":"A:Choose B:Back");}
  else if(screen==Screen::UploadReview){header("REVIEW EXTERNAL UPLOAD");text(5,36,uploadChoice==0?"Destination: WiGLE":"Destination: WDG Wars",theme().accent);wrap(5,50,fileNames[fileIndex],24,2);text(5,76,"Sends selected scan data",TFT_ORANGE);auto u=uploadStatus();text(5,88,String(u.message).substring(0,25));text(5,102,String(u.sent)+" / "+u.total+" bytes",theme().muted);footer("A:Send B:Back D:Cancel");}
  else if(screen==Screen::TrashConfirm){header("MOVE TO TRASH?");wrap(5,38,fileNames[fileIndex],24,3);text(5,78,"Recoverable in SD trash",theme().muted);footer("A:Move B:Cancel");}
  else if(screen==Screen::Playlist){header("SSID LIST EDITOR");String items[]={"Add SSID",String("Remove: ")+ssidName(playlistIndex),"Generate 10 random SSIDs","Save as beacon playlist","Load selected text file","Add observed SSIDs","Clear editor"};menuRows(items,7);footer("A:Choose L/R:SSID B:Back");}
  else if(screen==Screen::Mission){header(mission.active?"MISSION RECORDING":"RECON MISSIONS");String items[]={"Start WiFi + BLE mission","Start WiFi mission","Start BLE mission","Stop / save mission","Browse saved missions","Export Marauder AP JSON"};menuRows(items,6);footer(mission.active?"A:Choose / stop to finalize":"A:Choose B:Back");}
  else if(screen==Screen::MacSettings){header("RADIO MAC SETTINGS");String items[]={"Randomize AP MAC","Randomize station MAC","Restore factory AP MAC","Restore factory STA MAC","AP MAC = viewed target","STA MAC = viewed target"};menuRows(items,6);footer("Stop radios before change");}
  else if(screen==Screen::Music){header("MUSIC PLAYER");auto m=musicStatus();String name=m.path;int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);text(5,35,name.length()?name.substring(0,25):"SD /hackerphone/music",theme().accent);
    text(5,49,m.paused?"PAUSED":m.busy?"PLAYING":"STOPPED");text(5,61,String(m.elapsedMs/60000)+":"+(m.elapsedMs/1000%60<10?"0":"")+String(m.elapsedMs/1000%60)+"  Vol "+m.volume+"%");
    f.drawRect(5,75,150,5,theme().muted);if(m.size)f.fillRect(6,76,uint64_t(m.position)*148/m.size,3,theme().accent);
    text(5,87,String(m.message).substring(0,25),m.errors?TFT_ORANGE:theme().muted);text(5,101,m.repeat?"#:Repeat on  Up:Browse":"#:Repeat off Up:Browse",theme().muted);footer(m.busy?"A:Pause D:Stop L/R:Skip":"A:Play D:Stop L/R:Skip");}
  else if(screen==Screen::Leds){header("LEDS / NOTIFICATIONS");static const char* patterns[]={"Off","Solid","Breathe","Chase"};String items[16]={String("Pattern: ")+patterns[unsigned(ledSettings.pattern)],String("Brightness: ")+ledSettings.brightness,"Test LEDs / 5 seconds",String("Calls: ")+((ledSettings.notifications&1)?"on":"off"),String("Messages: ")+((ledSettings.notifications&2)?"on":"off"),String("Recon alerts: ")+((ledSettings.notifications&4)?"on":"off"),String("Tool completion: ")+((ledSettings.notifications&8)?"on":"off"),"Toggle all LEDs"};for(unsigned i=0;i<8;++i)items[i+8]=String("LED ")+String(i+1)+((ledSettings.mask&(1u<<i))?": enabled":": disabled");menuRows(items,16);footer("A:Change L/R:Level B:Back");}
  else if(screen==Screen::Settings){header("SETTINGS");String items[]={String("Theme: ")+theme().name,String("Radio: ")+(radioRunning?"running":"paused"),String("Scan: ")+modeName(),String("Channel: ")+(channelLock?String(channelLock):"hop 1-11"),String("CSV log: ")+(logging?"on":"off"),"Retry modem session",String("BLE scan: ")+(bleActiveScan?"active requests":"passive listen"),"Music player","LEDs / notifications","Test audio output","Lock keypad","Ringtones / RTTTL"};menuRows(items,12);footer("A:Change  B:Back");}
  else if(screen==Screen::Tools){header("SECURITY TOOLKIT");String items[]={"RSSI radar","WiFi access points","WiFi stations","Probe requests","BLE devices","Channel analyzer","Traffic alerts",String(storageStatus.recording?"Stop & save PCAP":"Record WiFi PCAP"),"Record EAPOL only","WiFi / BLE attacks",String("Scan: ")+modeName(),"Stop all radio tools","Dedicated scanners","Packet / BLE analyzer","Connected network tools","Saved device / SSID lists","Find My live sound sweep","Recon missions","SSID list editor","Radio MAC settings"};menuRows(items,20);footer("Up/Dn A:Open C:Home");}
  else if(screen==Screen::SavedLists){header("SD OBSERVATION LISTS");String items[]={"Save all observations","Save WiFi access points","Save BLE / trackers","Save SSIDs as playlist","Browse / load saved lists","Clear current observations"};menuRows(items,6);footer("A:Open B:Back");}
  else if(screen==Screen::Scanners){header("DEVICE / PROTOCOL SCANS");String items[unsigned(ScanProfile::Count)];for(unsigned i=0;i<unsigned(ScanProfile::Count);++i)items[i]=scanDefinitions[i].name;menuRows(items,unsigned(ScanProfile::Count));footer("A:Scan B:Back");}
  else if(screen==Screen::PacketMonitor&&handshakePage){header("EAPOL EXCHANGE MONITOR");auto& e=recon.eapol;
    text(4,36,String("M1 ")+e.messages[0]+" / M2 "+e.messages[1],theme().accent);text(4,51,String("M3 ")+e.messages[2]+" / M4 "+e.messages[3],theme().accent);
    text(4,68,String("Complete sequences ")+e.complete);text(4,83,String("PMKID frames ")+e.pmkids);text(4,101,"Observed, MIC not verified",theme().muted);footer("D:Graph B:Back");}
  else if(screen==Screen::PacketMonitor){header("PACKET / BLE ANALYZER");
    unsigned latest=(rateHead+47)%48;unsigned peak=1;for(unsigned i=0;i<48;++i)peak=std::max<unsigned>(peak,std::max(wifiRates[i],bleRates[i]));
    text(4,33,String("WiFi ")+wifiRates[latest]+"/s BLE "+bleRates[latest]+"/s",theme().accent);
    for(unsigned i=1;i<48;++i){unsigned a=(rateHead+i-1)%48,b=(rateHead+i)%48;
      f.drawLine(5+(i-1)*3,76-wifiRates[a]*27/peak,5+i*3,76-wifiRates[b]*27/peak,TFT_GREEN);
      f.drawLine(5+(i-1)*3,76-bleRates[a]*27/peak,5+i*3,76-bleRates[b]*27/peak,TFT_CYAN);}
    text(4,80,String("M ")+recon.managementFrames+" D "+recon.dataFrames+" C "+recon.controlFrames,theme().muted);
    text(4,92,String("EAPOL ")+recon.eapolFrames+" SAE "+recon.saeFrames,theme().muted);
    text(4,104,String(modeName())+" drop "+radioDrops,theme().accent);footer("D:EAPOL L/R:Radio B:Back");}
  else if(screen==Screen::Network){header("CONNECTED NETWORK TOOLS");String items[11+unsigned(NetworkProbe::Count)]={"SSID: "+networkSsid,"WiFi password (hidden)","Target: "+networkHost,"Join configured WiFi","Save WiFi profile","Load WiFi profile","Target the gateway","Start WiFi hotspot","Start captive portal","Status / scan results","Stop / disconnect"};for(unsigned i=0;i<unsigned(NetworkProbe::Count);++i)items[11+i]=networkProbeNames[i];menuRows(items,11+unsigned(NetworkProbe::Count));footer("A:Open B:Back C:Stop");}
  else if(screen==Screen::NetEdit){header(networkField==0?"EDIT WIFI SSID":networkField==1?"EDIT WIFI PASSWORD":networkField==2?"EDIT TARGET IPV4":networkField==3?"EDIT TARGET BSSID":networkField==4?"EDIT CLIENT MAC":networkField==5?"EDIT BEACON SSID":networkField==6?"ADD PLAYLIST SSID":networkField==7?"RENAME SELECTED FILE":"NEW SD FOLDER");
    String shown=networkEdit;if(networkField==1&&!networkShowPassword){shown="";for(unsigned i=0;i<networkEdit.length();++i)shown+='*';}wrap(4,34,shown,25,3);
    text(4,69,networkField==2?"Digits / * inserts dot":networkUpper?"Multi-tap ABC / *:case":"Multi-tap abc / *:case",theme().accent);
    text(4,83,"Up/Dn: last character",theme().muted);text(4,95,networkField==1?"#:space D:show password":"#:space / *:letter case",theme().muted);footer("A:Save B:Erase C:Cancel");}
  else if(screen==Screen::NetReview){header("REVIEW NETWORK TOOL");text(4,35,networkSelection==7?"WiFi hotspot":networkSelection==8?"Captive portal":networkProbeNames[networkSelection-11],theme().accent);
    wrap(4,50,networkSelection<11?networkSsid:(networkSelection==12||networkSelection==13||networkSelection>=16)?"Connected local subnet":networkHost,25,2);
    text(4,78,networkSelection<11?"Local AP, no Internet uplink":"Active requests to target");text(4,95,"D stops scan; C disconnects",theme().muted);footer("A:Start B:Cancel");}
  else if(screen==Screen::NetResults){header("NETWORK RESULTS");auto s=networkStatus();text(4,33,String(s.message).substring(0,25),theme().accent);text(4,45,String("IP ")+ipText(s.address),theme().muted);
    text(4,57,String("T ")+s.tested+" Found "+s.found+" Web "+s.visits);char line[64];if(s.rows){networkResult%=s.rows;if(networkRow(networkResult,line,sizeof(line)))wrap(4,72,line,25,3);text(4,105,String(networkResult+1)+"/"+s.rows+(s.truncated?" (first 64)":""),theme().muted);}footer("D:Stop #:Save B:Menu");}
  else if(screen==Screen::TrackerLiveConfirm){header("FIND MY LIVE SOUND");wrap(4,35,"15-second active sweep. Attempts a sound request on up to 8 nearby Find My / DULT trackers.",25,5);text(4,96,"Writes do not prove sound",theme().muted);footer("A:Start B:Cancel");}
  else if(screen==Screen::TrackerLiveRun){header("FIND MY LIVE SOUND");wrap(4,35,trackerLive.message,25,3);text(4,72,String("Attempted ")+trackerLive.attempted+" / 8");text(4,85,String("Sound writes ")+trackerLive.writes);text(4,100,trackerLive.active?"15s automatic stop":"Session ended",theme().muted);footer("A/B:Stop C:Home");}
  else if(screen==Screen::TrackerConfirm){header("TRACKER SOUND REQUEST");text(4,35,macText(recon.targets[targetIndex].mac),theme().accent);wrap(4,51,"Connect and request sound using AirTag, Find My or DULT service.",25,3);text(4,93,"Tracker may reject request",theme().muted);footer("A:Connect B:Cancel");}
  else if(screen==Screen::TargetTools){header("SELECTED DEVICE");String items[]={"Signal fox hunt","Connect / BLE GATT","Replay BLE advert","Detection evidence","Raw advertisement","Use as attack AP","Use as attack client","Tracker sound request","Join this WiFi network","Replay BLE payload + MAC","Portal from probe SSID","Record this AP: all","Record this AP: EAPOL"};menuRows(items,13);footer("A:Open B:Back");}
  else if(screen==Screen::TargetInfo){
    header(detailPage==0?"DETECTION EVIDENCE":detailPage==1?"RAW BLE ADVERTISEMENT":detailPage==2?"ACCESS POINT DETAILS":detailPage==3?"AP / CLIENT MAP":"REMOTE ID BROADCAST");auto& t=recon.targets[targetIndex];
    if(!detailPage){text(5,34,confidence(t.match.confidence),theme().accent);wrap(5,47,t.match.evidence,25,4);text(5,91,String("Age ")+(now-t.last)/1000+"s / peak "+t.peak+"dBm",theme().muted);text(5,103,t.ble?String("Payload changes ")+t.advertChanges:String(t.signal.quality()),theme().muted);}
    else if(detailPage==2){if(t.ble||!t.accessPoint)text(5,40,"Choose a WiFi access point");else{
      text(5,34,String("Security bits ")+t.security+" / WPS "+t.ap.wps);text(5,47,String("SAE ")+t.ap.sae+" PMF "+(t.ap.pmfRequired?"required":t.ap.pmfCapable?"capable":"absent"));
      text(5,60,String("Beacon ")+t.ap.interval+" TU / IEs "+t.ap.tags);text(5,73,String("SSID variants ")+t.ap.ssidCount+(t.ap.ssidCount==3?"+":""));
      text(5,86,String("Maker ")+(t.ap.manufacturer[0]?t.ap.manufacturer:"unadvertised"));text(5,99,"Values are self-reported",theme().muted);}}
    else if(detailPage==4){auto d=recon.drone(t);if(!d)wrap(5,40,"No decoded Remote ID for this transmitter.",25,3);else{
      text(5,34,d->hasId?String(d->id).substring(0,25):"ID not received",theme().accent);
      if(d->hasLocation){text(5,48,String(d->latitude,5)+", "+String(d->longitude,5));text(5,61,String("Geo alt ")+String(d->altitude,1)+"m / "+String((now-d->locationAt)/1000)+"s");}else text(5,48,"Position not received");
      if(d->hasOperator){text(5,75,"Operator "+String((now-d->operatorAt)/1000)+"s");text(5,88,String(d->operatorLatitude,5)+", "+String(d->operatorLongitude,5));}
      text(5,103,"Self-reported / unverified",theme().muted);}}
    else if(detailPage==3){if(t.ble)text(5,40,"WiFi association map only");else{const uint8_t* ap=t.station?t.bssid:t.mac;text(5,34,String("AP ")+macText(ap),theme().accent);unsigned row=0;
      for(auto& peer:recon.targets)if(peer.used&&!peer.ble&&peer.station&&!memcmp(peer.bssid,ap,6)){if(row<5)text(5,48+row*12,macText(peer.mac)+(peer.fresh(now)?" live":" old"));++row;}if(!row)text(5,50,"No client TX observed");}}
    else if(!t.ble)text(5,40,"WiFi: use PCAP recording");else{char b[32];snprintf(b,sizeof(b),"Company 0x%04X / %u bytes",t.company,t.advertSize);text(5,33,b,theme().accent);for(unsigned row=0;row<6;++row){String line;for(unsigned col=0;col<8&&row*8+col<t.advertSize;++col){char hex[4];snprintf(hex,sizeof(hex),"%02X ",t.advert[row*8+col]);line+=hex;}text(5,47+row*10,line);} }
    footer("Left/Right page B:Back");
  }
  else if(screen==Screen::Channels){
    header("CHANNEL OBSERVATIONS");float rates[12]{};float maximum=1;for(int i=1;i<=11;++i){rates[i]=recon.channelMillis[i]?1000.f*recon.channelFrames[i]/recon.channelMillis[i]:0;maximum=std::max(maximum,rates[i]);}
    for(int i=1;i<=11;++i){int h=std::clamp(int(rates[i]*42/maximum),1,42);f.fillRect(5+(i-1)*14,86-h,10,h,i==radioChannel?theme().accent:theme().grid);text(5+(i-1)*14,89,String(i));}
    text(5,33,String("RX/s peak ")+int(maximum),theme().muted);text(5,103,String("Channel: ")+(channelLock?String(channelLock):"auto hop"),theme().accent);footer("L/R:Channel  B:Back");
  }
  else if(screen==Screen::Alerts){
    header("TRAFFIC / ALERT HISTORY");text(5,33,String("Disconnects ")+recon.traffic.disconnectFrames+" / "+recon.traffic.disconnectAlerts,theme().accent);
    text(5,45,String("BLE bursts ")+recon.traffic.bleAlerts+" SSID "+recon.ssidAlerts,theme().accent);
    text(5,57,String("EAPOL ")+recon.eapolFrames+" probes "+recon.probes,theme().muted);
    if(recon.eventCount){unsigned n=std::min<uint32_t>(16,recon.eventCount);eventIndex%=n;auto& e=recon.events[(recon.eventCount-1-eventIndex)%16];wrap(5,72,e.text,25,3);text(5,104,String((now-e.at)/1000)+"s ago / "+(eventIndex+1)+" of "+n,theme().muted);}else text(5,78,"No burst alerts recorded");footer("Up/Down history B:Back");
  }
  else if(screen==Screen::Gatt){
    header("BLE GATT EXPLORER");auto g=gattStatus();text(5,33,String(g.message).substring(0,25),theme().accent);GattRow row;
    if(gattRow(gattIndex,row)){text(5,46,String(gattIndex+1)+"/"+g.rows+" handle "+String(row.handle,HEX));text(5,58,String(row.uuid).substring(0,25));text(5,70,String("R:")+bool(row.flags&1)+" W:"+bool(row.flags&2)+" N:"+bool(row.flags&4)+" I:"+bool(row.flags&8),theme().muted);}
    String hex;for(unsigned i=0;i<std::min<unsigned>(g.valueSize,16);++i){char b[4];snprintf(b,sizeof(b),"%02X ",g.value[i]);hex+=b;}wrap(5,83,hex,24,2);text(5,105,String("Notifications ")+g.notifications,theme().muted);footer("A:Read D:Sub #:Save B:Exit");
  }
  else if(screen==Screen::Attacks){header("WIFI / BLE ATTACKS");constexpr unsigned count=unsigned(AttackMode::Count);String items[count+3];for(unsigned i=0;i<count;++i)items[i]=attackDefinitions[i].name;items[count]="Target / list settings";items[count+1]="Named beacons / replay";items[count+2]="Stop transmission";menuRows(items,count+3);footer("A:Review B:Back C:Home");}
  else if(screen==Screen::AttackConfig){header("ATTACK TARGET SETTINGS");auto& t=attackOptions.target;String items[]={"AP "+macText(t.bssid),"Client "+macText(t.client),"SSID: "+String(t.ssid),"Channel: "+String(t.channel),"CSA destination: "+String(t.destinationChannel),"Clear selected client","Add target to list",String("Use list: ")+(attackOptions.useList?"yes / ":"no / ")+attackListCount(),"Clear target list","Add observed AP clients","Remove last target","Add marked APs / clients"};menuRows(items,12);footer("A:Edit B:Back");}
  else if(screen==Screen::AttackConfirm){
    header("REVIEW ATTACK");const auto& def=attackDefinitions[unsigned(pendingAttack)];auto& t=attackOptions.target;
    text(5,34,String(def.name).substring(0,25),theme().accent);
    bool list=attackOptions.useList&&!def.ble&&def.needsAp;
    text(5,47,list?String("Selected list: ")+attackListCount():def.ble?"Nearby BLE receivers":pendingAttack==AttackMode::SsidList?String(attackOptions.ssidPath).substring(13,38):String(t.ssid).substring(0,25));
    text(5,59,list?"250ms per target / # toggle":def.needsAp?macText(t.bssid):def.ble?"Pairing advertisement test":"Own radio / configured SSID",theme().muted);
    text(5,71,String(attackOptions.durationSeconds)+"s / "+attackOptions.packetsPerSecond+"pps / CH"+t.channel);
    text(5,83,t.hasClient?"Client "+macText(t.client):"Client: broadcast",theme().muted);
    char reason[64];bool ok=attackAvailable(pendingAttack,reason,sizeof(reason));text(5,98,String(reason).substring(0,25),ok?theme().accent:TFT_ORANGE);footer("A:Start D:Rate L/R:Time");
  }
  else if(screen==Screen::AttackRun){
    header("ATTACK SESSION");text(5,34,String(attackDefinitions[unsigned(attackStatus.mode)].name).substring(0,25),theme().accent);
    text(5,48,attackStatus.active?"TRANSMITTING":"STOPPED",attackStatus.active?TFT_ORANGE:theme().muted);
    text(5,61,String("Driver accepted ")+attackStatus.accepted);text(5,73,String("Errors ")+attackStatus.errors+" / EAPOL "+recon.eapolFrames);
    text(5,86,String(attackStatus.message).substring(0,25));text(5,101,"Effect needs verification",theme().muted);footer("A/B:Stop C:Home");
  }
  else if(screen==Screen::Lab){header("RADIO TEST TOOLS");String items[]={"WiFi beacon / 15s","BLE beacon / 15s","Stop transmission"};menuRows(items,3);text(5,78,lab.mode==LabMode::Off?"IDLE":"TRANSMITTING",lab.mode==LabMode::Off?theme().muted:TFT_ORANGE);text(5,90,String(lab.message).substring(0,25));text(5,103,String("Accepted ")+lab.accepted+" errors "+lab.errors,theme().muted);footer("A:Review B:Stop/back");}
  else if(screen==Screen::LabConfirm){header("REVIEW RADIO TEST");text(5,35,pendingLab==LabMode::WifiBeacon?"WiFi beacon transmission":pendingLab==LabMode::BleIdentityReplay?"BLE identity replay":pendingLab==LabMode::BleReplay?"BLE advert replay":"BLE beacon transmission",theme().accent);text(5,51,pendingLab==LabMode::BleIdentityReplay?macText(recon.targets[targetIndex].mac):pendingLab==LabMode::BleReplay?"Selected payload; own MAC":"Name: HackerPhone-Lab");text(5,65,"Duration: 15 seconds");text(5,79,pendingLab==LabMode::WifiBeacon?"10 frames/s, selected CH":"100ms advertising interval");text(5,96,"C or B stops the session",theme().muted);footer("A:Start B:Cancel");}
  else if(screen==Screen::Infrared){header("INFRARED");String items[]={"Record remote signal","Browse SD remotes","Current signal",String("Carrier: ")+String(irFrequency()/1000)+" kHz","Save current signal","Stop IR"};menuRows(items,6);footer("A:Open B:Back C:Home");}
  else if(screen==Screen::IrSignal){
    header("IR REMOTE");const auto* s=irSignal();
    if(irStatus.state==IrState::Learning){text(5,38,"RECORDING",theme().accent);text(5,54,"Point remote at IR module");text(5,67,"Press a button once");text(5,80,String(irFrequency()/1000)+" kHz assumed carrier");}
    else{ text(5,35,s?String(s->name).substring(0,25):"No signal selected",theme().accent);text(5,49,s?String(s->count)+" pulses / "+String(s->frequency/1000)+" kHz":"Record or browse SD");text(5,63,irStatus.library?"SD button #"+String(irStatus.record):irStatus.unsaved?"UNSAVED - D saves to SD":"Stored signal");text(5,77,irStatus.state==IrState::Sending?"TRANSMITTING":"A:Send once  D:Save",irStatus.state==IrState::Sending?TFT_ORANGE:theme().muted);}
    wrap(5,91,irStatus.message,25,2);footer(irBusy()?"A/B:Stop C:Home":"L/R:Button B:IR menu");
  }
  else if(screen==Screen::Incoming){header("INCOMING CALL");wrap(5,42,phone.state().caller);text(5,80,"Audio not yet validated",TFT_ORANGE);footer("A:Answer B:Reject");}
  else {header("ENGINEERING BUILD 0.4");text(5,35,String("Keys ")+(mp24::keysReady?"OK":"FAIL")+" / "+mp24::keyEvents);text(5,47,String("SD ")+(mp24::cardReady?"OK":"unavailable"));text(5,59,"Heap "+String(ESP.getFreeHeap()/1024)+"K");text(5,71,"PSRAM "+String(ESP.getFreePsram()/1024)+"K");text(5,83,"Modem "+String(phone.state().online?"OK":"starting/offline"));text(5,95,"WiFi "+String(recon.wifiFrames)+" BLE "+String(recon.bleFrames));footer("C:Home");}
  if(notice.length()&&uint32_t(now-noticeAt)<2500){f.fillRect(0,101,160,14,theme().selection);text(3,104,notice.substring(0,26),TFT_YELLOW);}
  if(volumeAt&&uint32_t(now-volumeAt)<1500){f.fillRoundRect(20,90,120,22,4,TFT_BLACK);text(35,97,String("Volume ")+musicStatus().volume+"%",theme().accent);}
  mp24::present();
}
static void serialCommands(uint32_t now){
  // Fits the 1024-byte RTTTL payload plus its command, or two full SD paths.
  static char line[1152];static unsigned n=0;static bool overflow=false;
  while(Serial.available()){char c=Serial.read();if(c=='\r')continue;if(c!='\n'){if(!overflow&&n+1<sizeof(line))line[n++]=c;else overflow=true;continue;}line[n]=0;n=0;if(overflow){overflow=false;Serial.println("HP_ERROR Command too long");continue;}
    if(shuttingDown){Serial.println("HP_SHUTTING_DOWN");continue;}
    if(!ready&&strcmp(line,"INFO")&&strcmp(line,"DISPLAY")){Serial.println("HP_NOT_READY Use INFO or DISPLAY");continue;}
    if(!strcmp(line,"INFO"))Serial.printf("HP_INFO v0.5.0-dev MP24 160x128 keys=%d events=%lu i2cerr=%lu sd=%d modem=%d sim=%u heap=%u psram=%u wifi=%lu ble=%lu drops=%lu\n",mp24::keysReady,(unsigned long)mp24::keyEvents,(unsigned long)mp24::i2cErrors,mp24::cardReady,phone.state().online,unsigned(phone.state().sim),ESP.getFreeHeap(),ESP.getFreePsram(),(unsigned long)recon.wifiFrames,(unsigned long)recon.bleFrames,(unsigned long)radioDrops);
    else if(!strcmp(line,"DISPLAY"))Serial.printf("HP_DISPLAY frames=%lu errors=%lu light=%d\n",(unsigned long)mp24::displayFrames,(unsigned long)mp24::displayErrors,mp24::lightReady);
    else if(!strcmp(line,"LIGHT")){mp24::backlight(true);Serial.printf("HP_LIGHT ready=%u\n",mp24::lightReady);}
    else if(!strcmp(line,"HEALTH"))Serial.printf("HP_HEALTH uptime=%lu stack_free=%u heap_ok=%d logging=%d log_bytes=%lu targets=%u\n",(unsigned long)now,unsigned(uxTaskGetStackHighWaterMark(nullptr)),heap_caps_check_integrity_all(false),logging,(unsigned long)(session?session.size():0),unsigned(recon.count(now)));
    else if(!strncmp(line,"FILE RENAME ",12)&&logging){Serial.println("HP_FILE FAILED Stop CSV logging first");}
    else if(!strncmp(line,"FILE TRASH ",11)&&logging){Serial.println("HP_FILE FAILED Stop CSV logging first");}
    else if(fileCommand(line)){}
    else if(toneCommand(line)){}
    else if(!strncmp(line,"UPLOAD WIGLE ",13))Serial.printf("HP_UPLOAD %s\n",uploadStart(0,line+13)?"STARTED":"FAILED");
    else if(!strncmp(line,"UPLOAD WDG ",11))Serial.printf("HP_UPLOAD %s\n",uploadStart(1,line+11)?"STARTED":"FAILED");
    else if(!strcmp(line,"UPLOAD CANCEL")){uploadCancel();Serial.println("HP_UPLOAD CANCELLED");}
    else if(!strcmp(line,"UPLOAD STATUS")){auto u=uploadStatus();Serial.printf("HP_UPLOAD busy=%u sent=%lu total=%lu http=%d accepted=%u message=%s\n",u.busy,(unsigned long)u.sent,(unsigned long)u.total,u.http,u.accepted,u.message);}

    else if(!strcmp(line,"TONES")){screen=Screen::Tones;menu=0;}
    else if(editorCommand(line)){}
    else if(!strcmp(line,"MUSIC")){screen=Screen::Music;}
    else if(!strcmp(line,"MUSIC STATUS")){auto m=musicStatus();Serial.printf("HP_MUSIC busy=%u paused=%u volume=%u rate=%lu channels=%u frames=%lu elapsed=%lu errors=%lu stack=%lu path=%s message=%s\n",m.busy,m.paused,m.volume,(unsigned long)m.rate,m.channels,(unsigned long)m.frames,(unsigned long)m.elapsedMs,(unsigned long)m.errors,(unsigned long)m.stackFree,m.path,m.message);}
    else if(!strncmp(line,"MUSIC PLAY ",11))Serial.printf("HP_MUSIC %s\n",!phone.state().phonePriority()&&musicPlay(line+11)?"QUEUED":"FAILED");
    else if(!strcmp(line,"MUSIC TEST"))Serial.printf("HP_MUSIC %s\n",!phone.state().phonePriority()&&musicTone()?"QUEUED":"FAILED");
    else if(!strcmp(line,"MUSIC STOP")){musicStop();nextMusic=0;Serial.println("HP_MUSIC STOPPING");}
    else if(!strcmp(line,"MUSIC PAUSE")){musicPause();Serial.println("HP_MUSIC TOGGLED");}
    else if(!strncmp(line,"VOLUME ",7)){unsigned value;char extra;if(sscanf(line+7,"%u%c",&value,&extra)==1&&value<=100){musicVolume(value);volumeAt=volumeSaveAt=now;Serial.println("HP_VOLUME OK");}else Serial.println("HP_VOLUME INVALID");}
    else if(!strcmp(line,"MISSION")){screen=Screen::Mission;menu=0;}
    else if(!strncmp(line,"MISSION START ",14)){unsigned mode;char extra;bool ok=sscanf(line+14,"%u%c",&mode,&extra)==1&&mode<=2&&!phone.state().phonePriority();if(ok){stopTools();if(logging){session.flush();session.close();logging=false;}ok=missionStart(mode);}Serial.printf("HP_MISSION %s path=%s\n",ok?"STARTED":"FAILED",mission.path);}
    else if(!strcmp(line,"MISSION STOP")){missionStop();Serial.println("HP_MISSION STOPPED");}
    else if(!strcmp(line,"MISSION STATUS"))Serial.printf("HP_MISSION active=%u records=%lu observations=%lu links=%lu probes=%lu alerts=%lu bytes=%lu error=%u path=%s\n",mission.active,(unsigned long)mission.records,(unsigned long)mission.observed,(unsigned long)mission.relationships,(unsigned long)mission.probes,(unsigned long)mission.alerts,(unsigned long)mission.bytes,mission.error,mission.path);
    else if(!strcmp(line,"PLAYLIST")){screen=Screen::Playlist;menu=0;}
    else if(!strcmp(line,"MACS")){screen=Screen::MacSettings;menu=0;}
    else if(!strcmp(line,"LEDS")){screen=Screen::Leds;menu=0;}
    else if(!strcmp(line,"LEDS TEST")){ledsTest(now);Serial.printf("HP_LEDS ready=%u\n",ledSettings.ready);}
    else if(!strcmp(line,"CONTROLS"))Serial.printf("HP_CONTROLS power=%u held=%lu locked=%u shutdown=%u leds=%u volume=%u keys=%lu\n",mp24::powerPressed(),(unsigned long)powerButton.held(now),locked,shuttingDown,ledSettings.ready,musicStatus().volume,(unsigned long)mp24::keyEvents);
    else if(!strcmp(line,"LOCK")){locked=true;mp24::backlight(false);Serial.println("HP_LOCKED");}
    else if(!strcmp(line,"UNLOCK")){locked=false;mp24::backlight(true);Serial.println("HP_UNLOCKED");}
    else if(!strcmp(line,"HOME")){irStop();trackerLiveStop();attackStop();labStop();networkStop();gattStop();screen=Screen::Home;menu=0;recon.selected=recon.pinned=-1;}
    else if(!strcmp(line,"BLE ACTIVE 0")||!strcmp(line,"BLE ACTIVE 1")){auto net=networkStatus();bool ok=!attackStatus.active&&lab.mode==LabMode::Off&&!trackerLive.active&&!gattStatus().busy&&!gattStatus().connected&&!net.busy&&!net.connected&&!net.hotspot;if(ok)radioSetBleActive(line[11]=='1');Serial.printf("HP_BLE_SCAN %s active=%u\n",ok?"OK":"BUSY",bleActiveScan);}
    else if(!strcmp(line,"RADIO STATUS")){uint8_t channel=0;wifi_second_chan_t secondary;esp_err_t error=esp_wifi_get_channel(&channel,&secondary);Serial.printf("HP_RADIO running=%u mode=%u logical=%u actual=%u error=%d\n",radioRunning,unsigned(radioMode),radioChannel,channel,error);}
    else if(!strncmp(line,"CHANNEL ",8)){char* end;unsigned long ch=strtoul(line+8,&end,10);auto net=networkStatus();bool ok=end!=line+8&&!*end&&ch<=11&&!attackStatus.active&&!net.connected&&!net.connecting&&!net.hotspot;
      if(ok){recon.selected=-1;channelLock=ch;}Serial.printf("HP_CHANNEL %s\n",ok?"OK":"FAILED");}
    else if(!strncmp(line,"APINFO ",7)){char* end;unsigned long i=strtoul(line+7,&end,10);if(end==line+7||*end||i>=ReconCore::Capacity||!recon.targets[i].accessPoint)Serial.println("HP_ERROR AP index");else{auto& t=recon.targets[i];Serial.printf("HP_APINFO channel=%u security=%u wps=%u sae=%u pmf_capable=%u pmf_required=%u variants=%u beacon_bytes=%u\n",t.channel,t.security,t.ap.wps,t.ap.sae,t.ap.pmfCapable,t.ap.pmfRequired,t.ap.ssidCount,unsigned(radioBeacon(t.mac,nullptr,0)));}}
    else if(!strcmp(line,"IR")){openInfrared();Serial.println("HP_IR MENU");}
    else if(!strcmp(line,"IR STATUS")){auto s=irSignal();Serial.printf("HP_IR state=%u count=%u hz=%lu unsaved=%u library=%u record=%u learned=%lu sent=%lu message=%s\n",unsigned(irStatus.state),s?s->count:0,(unsigned long)(s?s->frequency:irFrequency()),irStatus.unsaved,irStatus.library,irStatus.record,(unsigned long)irStatus.learned,(unsigned long)irStatus.sent,irStatus.message);}
    else if(!strcmp(line,"IR STOP")){irStop();Serial.println("HP_IR STOPPED");}
    else if(!strcmp(line,"IR SELFTEST")){Serial.printf("HP_IR_SELFTEST %s\n",irStorageTest()?"PASS":"FAIL");}
    else if(!strcmp(line,"IR RECORD")){openInfrared();bool ok=!phone.state().phonePriority()&&irLearn();if(ok)screen=Screen::IrSignal;Serial.printf("HP_IR %s %s\n",ok?"RECORDING":"FAILED",irStatus.message);}
    else if(!strcmp(line,"IR SEND")){bool ok=!phone.state().phonePriority()&&irSend();if(ok)screen=Screen::IrSignal;Serial.printf("HP_IR %s %s\n",ok?"SENDING":"FAILED",irStatus.message);}
    else if(!strcmp(line,"IR SAVE")||!strncmp(line,"IR SAVE ",8)){bool ok=irSave(strlen(line)>8?line+8:nullptr);Serial.printf("HP_IR %s %s\n",ok?"SAVED":"FAILED",irStatus.message);}
    else if(!strncmp(line,"IR OPEN ",8)){openInfrared();bool ok=irOpen(line+8);screen=Screen::IrSignal;Serial.printf("HP_IR %s %s\n",ok?"LOADED":"FAILED",irStatus.message);}
    else if(!strcmp(line,"IR NEXT")||!strcmp(line,"IR PREV")){bool ok=!strcmp(line,"IR NEXT")?irNext():irPrevious();Serial.printf("HP_IR %s %s\n",ok?"LOADED":"FAILED",irStatus.message);}
    else if(!strncmp(line,"IR FREQ ",8)){char* end;unsigned long hz=strtoul(line+8,&end,10);bool ok=end!=line+8&&!*end&&irSetFrequency(hz);Serial.printf("HP_IR %s\n",ok?"CONFIGURED":"FAILED");}
    else if(!strcmp(line,"TOOLS")){screen=Screen::Tools;menu=0;}
    else if(!strcmp(line,"SCANNERS")){screen=Screen::Scanners;menu=0;}
    else if(!strcmp(line,"SCANS LIST")){for(unsigned i=0;i<unsigned(ScanProfile::Count);++i)Serial.printf("HP_SCAN %u %s\n",i,scanDefinitions[i].name);Serial.println("HP_SCANS_END");}
    else if(!strncmp(line,"SCAN ",5)){char* end;unsigned long i=strtoul(line+5,&end,10);if(end!=line+5&&!*end&&i<unsigned(ScanProfile::Count)&&!phone.state().phonePriority()){chooseScanner(i);Serial.println("HP_SCAN_SELECTED");}else Serial.println("HP_ERROR scan index");}
    else if(!strcmp(line,"PACKETS")){if(startListening())screen=Screen::PacketMonitor;Serial.printf("HP_PACKETS mgmt=%lu data=%lu control=%lu beacon=%lu sae=%lu eapol=%lu\n",(unsigned long)recon.managementFrames,(unsigned long)recon.dataFrames,(unsigned long)recon.controlFrames,(unsigned long)recon.beacons,(unsigned long)recon.saeFrames,(unsigned long)recon.eapolFrames);}
    else if(!strcmp(line,"EAPOL STATUS")){auto& e=recon.eapol;Serial.printf("HP_EAPOL m1=%lu m2=%lu m3=%lu m4=%lu complete=%lu pmkid_frames=%lu\n",(unsigned long)e.messages[0],(unsigned long)e.messages[1],(unsigned long)e.messages[2],(unsigned long)e.messages[3],(unsigned long)e.complete,(unsigned long)e.pmkids);}
    else if(!strcmp(line,"LISTS")){screen=Screen::SavedLists;menu=0;Serial.println("HP_LISTS_MENU");}
    else if(!strcmp(line,"LIST SAVE ALL")||!strcmp(line,"LIST SAVE AP")||!strcmp(line,"LIST SAVE BLE")){bool ok=saveReconList(!strcmp(line,"LIST SAVE ALL")?0:!strcmp(line,"LIST SAVE AP")?1:4);Serial.printf("HP_LIST_SAVE %s %s\n",ok?"OK":"FAILED",ok?savedListPath:"");}
    else if(!strcmp(line,"LIST SAVE SSID")){bool ok=saveSsidList();if(ok)snprintf(attackOptions.ssidPath,sizeof(attackOptions.ssidPath),"%s",savedListPath);Serial.printf("HP_LIST_SAVE %s %s\n",ok?"OK":"FAILED",ok?savedListPath:"");}
    else if(!strncmp(line,"LIST LOAD ",10)){stopTools();bool ok=loadReconList(line+10);Serial.printf("HP_LIST_LOAD %s %s\n",ok?"OK":"FAILED",savedListMessage);}
    else if(!strncmp(line,"SSID FILE ",10)){const char* path=line+10;bool ok=!attackStatus.active&&strlen(path)<sizeof(attackOptions.ssidPath)&&!strncmp(path,"/hackerphone/",13)&&safeRelativePath(path+13);if(ok)snprintf(attackOptions.ssidPath,sizeof(attackOptions.ssidPath),"%s",path);Serial.printf("HP_SSID_FILE %s\n",ok?"OK":"FAILED");}
    else if(!strcmp(line,"NETWORK")){stopTools();screen=Screen::Network;menu=0;Serial.println("HP_NETWORK_MENU");}
    else if(!strcmp(line,"NETWORK PORTALTEST")){Serial.printf("HP_NETWORK_PORTALTEST %s\n",networkPortalTest()?"STARTED":"FAILED");}
    else if(!strcmp(line,"NETWORK SELFTEST")){bool ok=!phone.state().phonePriority()&&networkSelfTest();Serial.printf("HP_NETWORK_SELFTEST %s\n",ok?"STARTED":"FAILED");}
    else if(!strcmp(line,"NETWORK STATUS")){auto s=networkStatus();Serial.printf("HP_NETWORK connected=%u connecting=%u ap=%u portal=%u busy=%u ip=%s interface_up=%u tested=%lu found=%lu rows=%u visits=%lu forms=%lu saved=%lu lost=%lu message=%s\n",s.connected,s.connecting,s.hotspot,s.portal,s.busy,ipText(s.address).c_str(),s.interfaceUp,(unsigned long)s.tested,(unsigned long)s.found,s.rows,(unsigned long)s.visits,(unsigned long)s.forms,(unsigned long)s.formsSaved,(unsigned long)s.formsLost,s.message);}
    else if(!strcmp(line,"NETWORK STOP")){networkStop();Serial.println("HP_NETWORK_STOPPED");}
    else if(!strcmp(line,"NETWORK CANCEL")){networkCancelProbe();Serial.println("HP_NETWORK_CANCELLED");}
    else if(!strcmp(line,"NETWORK SAVE"))Serial.printf("HP_NETWORK_SAVE %s\n",networkSaveResults()?"OK":"FAILED");
    else if(!strcmp(line,"NETWORK RESULTS")){char row[64];for(unsigned i=0;networkRow(i,row,sizeof(row));++i)Serial.printf("HP_NET_ROW %u %s\n",i,row);Serial.println("HP_NET_END");}
    else if(!strncmp(line,"NETWORK SSID ",13)||!strncmp(line,"NETWORK PASSWORD ",17)||!strncmp(line,"NETWORK HOST ",13)){
      bool pass=!strncmp(line,"NETWORK PASSWORD ",17),host=!strncmp(line,"NETWORK HOST ",13);const char* value=line+(pass?17:13);size_t size=strlen(value);uint32_t ip;
      bool ok=pass?size<=63:host?parseIpv4(value,ip):size>0&&size<=32;if(ok){if(pass)networkPassword=value;else if(host)networkHost=value;else networkSsid=value;}Serial.printf("HP_NETWORK_CONFIG %s\n",ok?"OK":"FAILED");}
    else if(!strcmp(line,"NETWORK JOIN")||!strcmp(line,"NETWORK AP")||!strcmp(line,"NETWORK PORTAL")){
      trackerLiveStop();attackStop();labStop();gattStop();radioStop();captureStop();bool ok=!phone.state().phonePriority()&&!gattStatus().busy&&!gattStatus().connected;
      if(ok)ok=!strcmp(line,"NETWORK JOIN")?networkJoin(networkSsid.c_str(),networkPassword.c_str()):networkHotspot(networkSsid.c_str(),networkPassword.c_str(),!strcmp(line,"NETWORK PORTAL"));
      if(ok){screen=Screen::NetResults;networkResult=0;}Serial.printf("HP_NETWORK_START %s\n",ok?"OK":"FAILED");}
    else if(!strncmp(line,"NETWORK PROBE ",14)){char* end;unsigned long mode=strtoul(line+14,&end,10);bool ok=end!=line+14&&!*end&&mode<unsigned(NetworkProbe::Count)&&!phone.state().phonePriority()&&networkProbe(NetworkProbe(mode),networkHost.c_str());if(ok){screen=Screen::NetResults;networkResult=0;}Serial.printf("HP_NETWORK_PROBE %s\n",ok?"STARTED":"FAILED");}
    else if(!strcmp(line,"TRACKER LIVE STOP")){trackerLiveStop();Serial.println("HP_TRACKER_LIVE STOPPED");}
    else if(!strcmp(line,"TRACKER LIVE STATUS")){Serial.printf("HP_TRACKER_LIVE active=%u attempts=%u writes=%u message=%s\n",trackerLive.active,trackerLive.attempted,trackerLive.writes,trackerLive.message);}
    else if(!strcmp(line,"TRACKER LIVE START")){bool ok=!phone.state().phonePriority()&&trackerLiveStart(15);if(ok)screen=Screen::TrackerLiveRun;Serial.printf("HP_TRACKER_LIVE %s\n",ok?"STARTED":"FAILED");}
    else if(!strncmp(line,"TRACKER SOUND ",14)){char* end;unsigned long i=strtoul(line+14,&end,10);bool ok=end!=line+14&&!*end&&i<ReconCore::Capacity&&recon.targets[i].ble&&recon.targets[i].fresh(now)&&!attackStatus.active&&lab.mode==LabMode::Off&&!trackerLive.active&&!phone.state().phonePriority()&&gattTrackerSound(recon.targets[i].mac,recon.targets[i].addressType);Serial.printf("HP_TRACKER_SOUND %s\n",ok?"QUEUED":"FAILED");}
    else if(!strcmp(line,"ATTACKS")){screen=Screen::Attacks;menu=0;}
    else if(!strcmp(line,"ATTACK LIST")){for(unsigned i=0;i<unsigned(AttackMode::Count);++i)Serial.printf("HP_ATTACK_MODE %u %s\n",i,attackDefinitions[i].name);Serial.println("HP_ATTACK_END");}
    else if(!strcmp(line,"ATTACK TARGET ADD"))Serial.printf("HP_ATTACK_TARGET %s\n",attackListAdd()?"ADDED":"FAILED");
    else if(!strcmp(line,"ATTACK TARGET CLEAR")){attackListClear();Serial.printf("HP_ATTACK_TARGET count=%u\n",attackListCount());}
    else if(!strcmp(line,"ATTACK TARGET LIST")){AttackTarget target;for(unsigned i=0;attackListGet(i,target);++i)Serial.printf("HP_ATTACK_TARGET %u ap=%s client=%s selected=%u channel=%u ssid=%s\n",i,macText(target.bssid).c_str(),macText(target.client).c_str(),target.hasClient,target.channel,target.ssid);Serial.println("HP_ATTACK_TARGET_END");}
    else if(!strcmp(line,"ATTACK USELIST 0")||!strcmp(line,"ATTACK USELIST 1")){bool ok=!attackStatus.active;if(ok)attackOptions.useList=line[15]=='1';Serial.printf("HP_ATTACK %s\n",ok?"OK":"BUSY");}
    else if(!strncmp(line,"ATTACK MAC ",11)||!strncmp(line,"ATTACK CLIENTMAC ",17)){bool client=!strncmp(line,"ATTACK CLIENTMAC ",17);uint8_t mac[6];bool ok=!attackStatus.active&&parseMac(line+(client?17:11),mac)&&(!client||attackOptions.target.hasAp);if(ok){if(client){memcpy(attackOptions.target.client,mac,6);attackOptions.target.hasClient=true;}else{memcpy(attackOptions.target.bssid,mac,6);attackOptions.target.hasAp=true;attackOptions.target.hasClient=false;}}Serial.printf("HP_ATTACK %s\n",ok?"OK":"FAILED");}
    else if(!strcmp(line,"ATTACK STATUS")){
      auto& t=attackOptions.target;Serial.printf("HP_ATTACK active=%u mode=%u accepted=%lu errors=%lu driver=%d adapter=%u challenges=%lu responses=%lu message=%s\n",attackStatus.active,unsigned(attackStatus.mode),(unsigned long)attackStatus.accepted,(unsigned long)attackStatus.errors,attackStatus.lastError,rawFrameAdapterLinked(),(unsigned long)attackStatus.challenges,(unsigned long)attackStatus.responses,attackStatus.message);
      Serial.printf("HP_ATTACK_TARGET ap=%u bssid=%s channel=%u client=%s time=%u rate=%u ssid=%s\n",t.hasAp,macText(t.bssid).c_str(),t.channel,t.hasClient?macText(t.client).c_str():"broadcast",attackOptions.durationSeconds,attackOptions.packetsPerSecond,t.ssid);
    }
    else if(!strcmp(line,"ATTACK STOP")){attackStop();Serial.println("HP_ATTACK STOPPED");}
    else if(!strcmp(line,"ATTACK CLEAR CLIENT")){if(!attackStatus.active){attackOptions.target.hasClient=false;memset(attackOptions.target.client,0,6);}Serial.printf("HP_ATTACK %s\n",attackStatus.active?"BUSY":"CLIENT_CLEARED");}
    else if(!strncmp(line,"ATTACK SSID ",12)){
      size_t len=strlen(line+12);bool ok=!attackStatus.active&&len&&len<=32;
      if(ok)memcpy(attackOptions.target.ssid,line+12,len+1);Serial.printf("HP_ATTACK %s\n",ok?"CONFIGURED":"FAILED");
    }
    else if(!strncmp(line,"ATTACK ",7)){
      char* argument=strrchr(line,' ');char* end=nullptr;unsigned long value=argument?strtoul(argument+1,&end,10):0;
      bool valid=argument&&end!=argument+1&&!*end&&!attackStatus.active;bool ok=false;
      if(valid&&!strncmp(line,"ATTACK AP ",10)&&argument==line+9)ok=attackSelectAp(value);
      else if(valid&&!strncmp(line,"ATTACK CLIENT ",14)&&argument==line+13)ok=attackSelectClient(value);
      else if(valid&&!strncmp(line,"ATTACK TIME ",12)&&argument==line+11&&value>=1&&value<=60){attackOptions.durationSeconds=value;ok=true;}
      else if(valid&&!strncmp(line,"ATTACK RATE ",12)&&argument==line+11&&value>=1&&value<=100){attackOptions.packetsPerSecond=value;ok=true;}
      else if(valid&&!strncmp(line,"ATTACK CHANNEL ",15)&&argument==line+14&&value>=1&&value<=11){attackOptions.target.channel=value;ok=true;}
      else if(valid&&!strncmp(line,"ATTACK DEST ",12)&&argument==line+11&&value>=1&&value<=11){attackOptions.target.destinationChannel=value;ok=true;}
      else if(valid&&!strncmp(line,"ATTACK START ",13)&&argument==line+12&&value<unsigned(AttackMode::Count)&&!phone.state().phonePriority()){pendingAttack=AttackMode(value);ok=attackStart(pendingAttack);if(ok)screen=Screen::AttackRun;}
      Serial.printf("HP_ATTACK %s\n",ok?"OK":"FAILED");
    }
    else if(!strcmp(line,"FILES")){screen=Screen::Files;irBrowsing=false;if(mp24::cardReady)files();}
    else if(!strcmp(line,"SETTINGS")){screen=Screen::Settings;menu=0;}
    else if(!strcmp(line,"CHANNELS")){screen=Screen::Channels;}
    else if(!strcmp(line,"ALERTS")){screen=Screen::Alerts;}
    else if(!strncmp(line,"THEME ",6)&&strlen(line)==7&&line[6]>='0'&&line[6]<'0'+ThemeCount){setTheme(line[6]-'0');Serial.printf("HP_THEME %u %s\n",themeIndex,theme().name);}
    else if(!strcmp(line,"TOOLKIT")){auto g=gattStatus();Serial.printf("HP_TOOLKIT theme=%u mode=%u lock=%u eapol=%lu probes=%lu disconnects=%lu wifi_alerts=%lu ble_alerts=%lu ssid_alerts=%lu pcap=%u records=%lu bytes=%lu drops=%lu sd_error=%u vendors=%lu gatt=%u busy=%u tx=%u\n",themeIndex,unsigned(radioMode),channelLock,(unsigned long)recon.eapolFrames,(unsigned long)recon.probes,(unsigned long)recon.traffic.disconnectFrames,(unsigned long)recon.traffic.disconnectAlerts,(unsigned long)recon.traffic.bleAlerts,(unsigned long)recon.ssidAlerts,storageStatus.recording,(unsigned long)storageStatus.records,(unsigned long)storageStatus.bytes,(unsigned long)storageStatus.drops,storageStatus.error,(unsigned long)storageStatus.vendors,g.connected,g.busy,unsigned(lab.mode));}
    else if(!strcmp(line,"STOP")){stopTools();Serial.println("HP_STOPPED");}
    else if(!strcmp(line,"LAB WIFI")||!strcmp(line,"LAB BLE")){bool ok=!phone.state().phonePriority()&&labStart(!strcmp(line,"LAB WIFI")?LabMode::WifiBeacon:LabMode::BleBeacon,channelLock?channelLock:1);if(ok){screen=Screen::Lab;menu=2;}Serial.printf("HP_LAB %s\n",ok?"STARTED":"FAILED");}
    else if(!strcmp(line,"LAB STATUS"))Serial.printf("HP_LAB mode=%u accepted=%lu errors=%lu message=%s\n",unsigned(lab.mode),(unsigned long)lab.accepted,(unsigned long)lab.errors,lab.message);
    else if(!strncmp(line,"MODE ",5)&&strlen(line)==6&&line[5]>='0'&&line[5]<='2'){auto n=networkStatus();if(attackStatus.active||gattStatus().busy||gattStatus().connected||lab.mode!=LabMode::Off||trackerLive.active||n.busy||n.connected||n.connecting||n.hotspot)Serial.println("HP_ERROR Stop active tool first");else{radioSetMode(RadioMode(line[5]-'0'));Serial.println("HP_MODE_OK");}}
    else if(!strncmp(line,"PCAP AP ",8)||!strncmp(line,"PCAP EAPOLAP ",13)){bool only=!strncmp(line,"PCAP EAPOLAP ",13);const char* value=line+(only?13:8);char* end;unsigned long i=strtoul(value,&end,10);bool ok=end!=value&&!*end&&i<ReconCore::Capacity&&recon.targets[i].accessPoint&&!recon.targets[i].ble&&recon.targets[i].fresh(now);if(ok){auto& t=recon.targets[i];captureStop();radioMode=RadioMode::Wifi;channelLock=t.channel;ok=startListening()&&captureStart(only,t.mac);}Serial.printf("HP_PCAP %s %s\n",ok?"STARTED":"FAILED",ok?storageStatus.path:"");}
    else if(!strcmp(line,"PCAP START")||!strcmp(line,"PCAP EAPOL")){bool ok=startListening()&&captureStart(!strcmp(line,"PCAP EAPOL"));Serial.printf("HP_PCAP %s %s\n",ok?"STARTED":"FAILED",ok?storageStatus.path:"");}
    else if(!strcmp(line,"PCAP STOP")){captureStop();Serial.printf("HP_PCAP SAVED %s bytes=%lu records=%lu drops=%lu error=%u\n",storageStatus.path,(unsigned long)storageStatus.bytes,(unsigned long)storageStatus.records,(unsigned long)storageStatus.drops,storageStatus.error);}
    else if(!strcmp(line,"TARGETS")){for(unsigned i=0;i<ReconCore::Capacity;++i){auto& t=recon.targets[i];if(t.used)Serial.printf("HP_TARGET %u %s %s type=%u rssi=%d family=%s name=%s live=%u age_ms=%lu\n",i,t.ble?"BLE":"WiFi",macText(t.mac).c_str(),t.addressType,t.signal.value(),dragon::familyName(t.match.family),t.name,t.fresh(now),(unsigned long)(now-t.last));}Serial.println("HP_TARGETS_END");}
    else if(!strncmp(line,"BLEAD ",6)){char* end;unsigned long i=strtoul(line+6,&end,10);if(end==line+6||*end||i>=ReconCore::Capacity||!recon.targets[i].used||!recon.targets[i].ble)Serial.println("HP_ERROR BLE target index");else{auto& t=recon.targets[i];Serial.printf("HP_AD %lu ",i);for(unsigned j=0;j<t.advertSize;++j)Serial.printf("%02X",t.advert[j]);Serial.println();}}
    else if(!strcmp(line,"GATT STATUS")){auto g=gattStatus();Serial.printf("HP_GATT connected=%u busy=%u rows=%u truncated=%u notifications=%lu message=%s\n",g.connected,g.busy,g.rows,g.truncated,(unsigned long)g.notifications,g.message);Serial.printf("HP_VALUE handle=%04X hex=",g.valueHandle);for(unsigned i=0;i<g.valueSize;++i)Serial.printf("%02X",g.value[i]);Serial.println();}
    else if(!strcmp(line,"GATT DUMP")){GattRow row;for(unsigned i=0;gattRow(i,row);++i)Serial.printf("HP_GATT_ROW %u %04X %s %s flags=%u\n",i,row.handle,row.service,row.uuid,row.flags);Serial.println("HP_GATT_END");}
    else if(!strcmp(line,"GATT STOP")){gattStop();Serial.println("HP_GATT_STOP");}
    else if(!strncmp(line,"GATT CONNECT ",13)){char* end;unsigned long i=strtoul(line+13,&end,10);bool ok=end!=line+13&&!*end&&i<ReconCore::Capacity&&recon.targets[i].ble&&recon.targets[i].fresh(now)&&!attackStatus.active&&lab.mode==LabMode::Off&&!trackerLive.active&&!phone.state().phonePriority()&&gattConnect(recon.targets[i].mac,recon.targets[i].addressType);Serial.printf("HP_GATT %s\n",ok?"QUEUED":"FAILED");}
    else if(!strncmp(line,"GATT READ ",10)||!strncmp(line,"GATT SUB ",9)||!strncmp(line,"GATT UNSUB ",11)){
      unsigned offset=!strncmp(line,"GATT READ ",10)?10:!strncmp(line,"GATT SUB ",9)?9:11;char* end;unsigned long handle=strtoul(line+offset,&end,16);bool ok=end!=line+offset&&!*end&&handle&&handle<=65535&&!phone.state().phonePriority();if(ok)ok=offset==10?gattRead(handle):gattSubscribe(handle,offset==9);Serial.printf("HP_GATT %s\n",ok?"QUEUED":"FAILED");}
    else if(!strncmp(line,"GATT WRITE ",11)){
      char* end;unsigned long handle=strtoul(line+11,&end,16);uint8_t bytes[64];size_t size=0;bool ok=end!=line+11&&*end==' '&&handle&&handle<=65535&&!phone.state().phonePriority();
      if(ok){const char* hex=end+1;size_t length=strlen(hex);ok=length&&length%2==0&&length<=128;for(size_t i=0;ok&&i<length;i+=2){auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;};int a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0)ok=false;else bytes[size++]=(a<<4)|b;}}
      if(ok)ok=gattWrite(handle,bytes,size,true);Serial.printf("HP_GATT %s\n",ok?"QUEUED":"FAILED");}
    else if(!strncmp(line,"FILEINFO ",9)){
      const char* relative=line+9;if(!safeSdPath((String("/hackerphone/")+relative).c_str()))Serial.println("HP_FILE_ERROR path");else{File f=SD.open(String("/hackerphone/")+relative,FILE_READ);if(!f||f.isDirectory())Serial.println("HP_FILE_ERROR missing");else Serial.printf("HP_FILEINFO %lu\n",(unsigned long)f.size());f.close();}}
    else if(!strncmp(line,"FILECHUNK ",10)){
      char* separator=strrchr(line+10,' ');if(!separator){Serial.println("HP_FILE_ERROR syntax");continue;}*separator=0;char* end;unsigned long offset=strtoul(separator+1,&end,10);
      if(end==separator+1||*end||!safeSdPath((String("/hackerphone/")+(line+10)).c_str())){Serial.println("HP_FILE_ERROR path/offset");continue;}
      File f=SD.open(String("/hackerphone/")+(line+10),FILE_READ);if(!f||f.isDirectory()||offset>f.size()||!f.seek(offset)){Serial.println("HP_FILE_ERROR read");f.close();continue;}
      uint8_t bytes[1024];int got=f.read(bytes,std::min<size_t>(sizeof(bytes),f.size()-offset));f.close();if(got<0){Serial.println("HP_FILE_ERROR IO");continue;}Serial.printf("HP_CHUNK %lu %d\n",offset,got);Serial.write(bytes,got);Serial.println("\nHP_CHUNK_END");}
    else if(!strcmp(line,"RECON"))startRecon();
    else if(!strcmp(line,"PAUSE")){attackStop();radioStop();}
    else if(!strcmp(line,"HARDWARE"))screen=Screen::Diagnostics;
    else if(!strcmp(line,"RETRYMODEM"))phone.begin(now);
    else if(!strcmp(line,"SDTEST")){
      bool ok=mp24::cardReady;char path[80];snprintf(path,sizeof(path),"/hackerphone/selftest-%08lx.tmp",(unsigned long)now);
      if(ok&&!SD.exists("/hackerphone"))ok=SD.mkdir("/hackerphone");
      if(ok&&SD.exists(path))ok=false;
      bool created=false;const char payload[]="HackerPhone dedicated SD write/read test\n";
      if(ok){File f=SD.open(path,FILE_WRITE);created=bool(f);ok=created&&f.write((const uint8_t*)payload,sizeof(payload))==sizeof(payload);f.flush();f.close();}
      if(ok){File f=SD.open(path);char readback[sizeof(payload)]{};ok=f&&f.size()==sizeof(payload)&&f.readBytes(readback,sizeof(readback))==sizeof(payload)&&!memcmp(readback,payload,sizeof(payload));f.close();}
      if(created)SD.remove(path);
      Serial.printf("HP_SDTEST %s\n",ok?"PASS":"FAIL");
    }
    else if(!strcmp(line,"SCREEN")){render(now);Serial.setTxTimeoutMs(250);Serial.printf("HP_SCREEN 160 128 40960\n");Serial.flush();uint8_t row[320];bool ok=true;for(int y=0;y<128;++y){for(int x=0;x<160;++x){uint16_t pixel=mp24::frame.readPixel(x,y);row[x*2]=pixel>>8;row[x*2+1]=pixel;}if(Serial.write(row,sizeof(row))!=sizeof(row)){ok=false;break;}}Serial.print(ok?"\nHP_SCREEN_END\n":"\nHP_SCREEN_ERROR Short USB write\n");Serial.setTxTimeoutMs(20);}
    else Serial.println("Commands: INFO HOME RECON PAUSE HARDWARE RETRYMODEM SCREEN");
  }
}
void setup(){
  preferences.begin("hackerphone",false);themeIndex=preferences.getUChar("theme",0)%ThemeCount;bleActiveScan=preferences.getBool("bleactive",false);
  Serial.setRxBufferSize(2048);Serial.begin(115200);Serial.setTxTimeoutMs(20);Serial.setDebugOutput(true);
  delay(250);Serial.println("HP_STAGE usb");
  ready=mp24::begin();
  if(!ready){Serial.println("HP_FAIL framebuffer");return;}
  irBegin();ledsStart();tonesBegin();musicBegin(preferences.getUChar("volume",35),preferences.getBool("repeat",false));
  auto& f=mp24::frame;f.fillSprite(TFT_BLACK);f.setSwapBytes(true);f.pushImage((160-bwmWidth)/2,7,bwmWidth,bwmHeight,bwmPixels);text(44,91,"HACKERPHONE");text(19,107,"Engineering preview",theme().muted);mp24::present();
  storageBegin();mp24::modemBegin();delay(1200);Serial.println("HP_READY MP24 engineering preview 0.5.0-dev");
}
void loop(){if(!ready){serialCommands(millis());delay(10);return;}uint32_t now=millis();if(!shuttingDown)mp24::modemPoll(phone,now);serialCommands(now);now=millis();key(mp24::key(now),now);now=millis();
  auto pe=powerButton.update(mp24::powerPressed(),now);
  if(!shuttingDown&&pe==PowerEvent::Lock){locked=!locked;mp24::backlight(!locked);}
  if(!shuttingDown&&pe==PowerEvent::Shutdown){shuttingDown=true;shutdownAt=now;fileCancel();locked=false;stopTools();musicStop();nextMusic=0;if(logging){session.flush();session.close();logging=false;}preferences.putUChar("volume",musicStatus().volume);mp24::modemShutdown();mp24::backlight(true);}
  if(shuttingDown){musicTick();ledsTick(now,false,true);if(uint32_t(now-shutdownAt)>3000&&!musicStatus().busy&&!networkStatus().busy){if(!cutRequested)cutRequested=mp24::powerCut();if(cutRequested&&!mp24::powerPressed()){mp24::backlight(false);esp_sleep_enable_ext0_wakeup(GPIO_NUM_2,1);Serial.flush();esp_deep_sleep_start();}}if(uint32_t(now-lastDraw)>100){lastDraw=now;render(now);}delay(5);return;}
  if(volumeSaveAt&&uint32_t(now-volumeSaveAt)>1500){preferences.putUChar("volume",musicStatus().volume);volumeSaveAt=0;}
  if(nextMusic&&!musicStatus().busy){int step=nextMusic;nextMusic=0;musicNext(step);}
  if(phone.state().phonePriority()){irStop();trackerLiveStop();attackStop();networkStop();if(radioRunning)radioStop();if(lab.mode!=LabMode::Off)labStop();auto gs=gattStatus();if(gs.busy||gs.connected)gattStop();if(phone.state().call==Call::Ringing)screen=Screen::Incoming;}
  static uint32_t lastSms=0,lastEvents=0;static bool wasRinging=false,wasCapture=false;
  bool ringing=phone.state().call==Call::Ringing;if(ringing&&!wasRinging)ledsNotify(0,now);wasRinging=ringing;
  if(phone.state().inboxNotifications!=lastSms){lastSms=phone.state().inboxNotifications;ledsNotify(1,now);}
  if(recon.eventCount!=lastEvents){lastEvents=recon.eventCount;ledsNotify(2,now);}
  if(wasCapture&&!storageStatus.recording){ledsNotify(3,now);if(toneEnabled&8)tonePreview(3);}wasCapture=storageStatus.recording;
  tonesTick(phone.state(),now,recon.eventCount);musicTick();ledsTick(now,locked);fileTick(now);
  radioTick(now);trackerLiveTick(now);labTick(now);attackTick(now);irTick(now);networkTick(now);storageTick(now);saveTargets(now);sampleRates(now);missionTick(now);
  if(uint32_t(now-lastDraw)>=100){lastDraw=now;render(now);}delay(2);
}
