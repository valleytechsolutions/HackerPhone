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

using namespace hackerphone;
static mp24::ModemTransport transport;
static PhoneCore phone(transport);
enum class Screen {Home,Dial,Messages,Compose,ConfirmSms,Recon,Targets,Hunt,Files,FileView,Settings,Diagnostics,Incoming,Tools,Channels,Alerts,TargetTools,TargetInfo,Gatt,Lab,LabConfirm,Attacks,AttackConfirm,AttackRun,Infrared,IrSignal};
static Screen screen=Screen::Home;
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
static LabMode pendingLab=LabMode::Off;
static AttackMode pendingAttack=AttackMode::Beacon;
static char vendorName[48]{};
static const char* modeName(){return radioMode==RadioMode::Combined?"WiFi + BLE":radioMode==RadioMode::Wifi?"WiFi only":"BLE only";}
static void setTheme(unsigned index){themeIndex=index%ThemeCount;preferences.putUChar("theme",themeIndex);}
static bool targetVisible(const Target& t){return t.used&&(targetFilter==0||(targetFilter==1&&!t.ble&&t.accessPoint)||(targetFilter==2&&!t.ble&&t.station)||(targetFilter==3&&!t.ble&&t.probe)||(targetFilter==4&&t.ble));}
static void cacheVendor(){vendorName[0]=0;auto& t=recon.targets[targetIndex];if(t.used)lookupVendor(t.mac,t.ble,t.addressType,vendorName);}
static void nextTarget(int step){for(unsigned i=0;i<ReconCore::Capacity;++i){targetIndex=(targetIndex+ReconCore::Capacity+step)%ReconCore::Capacity;if(targetVisible(recon.targets[targetIndex]))break;}recon.pinned=targetVisible(recon.targets[targetIndex])?targetIndex:-1;cacheVendor();}
static bool startListening(){irStop();attackStop();labStop();auto g=gattStatus();if(g.busy||g.connected){gattStop();return false;}return radioStart();}
static void stopTools(){irStop();attackStop();labStop();gattStop();radioStop();captureStop();recon.selected=recon.pinned=-1;}
static void openInfrared(){stopTools();if(logging){session.flush();session.close();logging=false;}screen=Screen::Infrared;menu=0;}

static const char* simName(){switch(phone.state().sim){case Sim::Absent:return "NO SIM";case Sim::Ready:return phone.state().registered()?"REGISTERED":"SEARCHING";case Sim::PinRequired:return "SIM PIN";case Sim::PukRequired:return "SIM PUK";case Sim::Fault:return "SIM ERROR";default:return phone.state().online?"CHECKING SIM":"MODEM START";}}
static void notify(const char* text){notice=text;noticeAt=millis();}
static uint16_t color(uint32_t c){return mp24::lcd.color565(c>>16,c>>8,c);}
static void text(int x,int y,const String& s,uint16_t fg=TFT_WHITE){mp24::frame.setTextColor(fg);mp24::frame.drawString(s,x,y,1);}
static void wrap(int x,int y,const String& s,int width=24,int maxLines=7){for(int line=0;line<maxLines&&size_t(line*width)<s.length();++line)text(x,y+line*10,s.substring(line*width,(line+1)*width));}
static void header(const char* title){auto& f=mp24::frame;f.fillSprite(theme().background);f.fillRect(0,0,160,15,theme().bar);text(5,4,title);text(5,18,simName(),theme().muted);text(102,18,attackStatus.active||lab.mode!=LabMode::Off?"TX ACTIVE":storageStatus.recording?"REC PCAP":radioRunning?(radioBle?"BLE RX":"WiFi RX"):"IDLE",storageStatus.recording||attackStatus.active||lab.mode!=LabMode::Off?TFT_ORANGE:theme().accent);f.drawFastHLine(0,29,160,0x39e7);}
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
  if(attackStatus.active&&screen!=Screen::AttackRun)attackStop();
  if(unsigned(k)<=unsigned(mp24::K9)){digitInput(unsigned(k),now);return;}
  if(k==mp24::C){irStop();attackStop();labStop();gattStop();screen=Screen::Home;menu=0;recon.selected=recon.pinned=-1;return;}
  bool accept=k==mp24::A||k==mp24::Enter;
  if(screen==Screen::Home){if(k==mp24::Down||k==mp24::Right)menu=(menu+1)%7;if(k==mp24::Up||k==mp24::Left)menu=(menu+6)%7;if(accept){switch(menu){case 0:screen=Screen::Dial;break;case 1:screen=Screen::Messages;break;case 2:screen=Screen::Tools;menu=0;break;case 3:openInfrared();break;case 4:screen=Screen::Files;irBrowsing=false;if(mp24::cardReady)files();break;case 5:screen=Screen::Settings;menu=0;break;case 6:screen=Screen::Diagnostics;break;}}return;}
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
    moveMenu(k,12);
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
    }}if(k==mp24::B){screen=Screen::Home;menu=0;}return;
  }
  if(screen==Screen::Targets){
    if(k==mp24::Down||k==mp24::Right)nextTarget(1);if(k==mp24::Up||k==mp24::Left)nextTarget(-1);
    if(targetVisible(recon.targets[targetIndex])){if(accept){recon.selected=targetIndex;screen=Screen::Hunt;}if(k==mp24::D){screen=Screen::TargetTools;menu=0;}}
    if(k==mp24::B){screen=Screen::Tools;menu=0;recon.pinned=-1;}return;
  }
  if(screen==Screen::TargetTools){
    moveMenu(k,7);auto& t=recon.targets[targetIndex];
    if(accept){switch(menu){case 0:recon.selected=targetIndex;screen=Screen::Hunt;break;
      case 1:attackStop();labStop();if(t.ble&&t.fresh(now)&&gattConnect(t.mac,t.addressType)){screen=Screen::Gatt;gattIndex=0;}else notify("Choose a live BLE device");break;
      case 2:if(t.ble&&validLegacyAdvert(t.advert,t.advertSize)){pendingLab=LabMode::BleReplay;screen=Screen::LabConfirm;}else notify("Needs complete <=31B advert");break;
      case 3:detailPage=0;screen=Screen::TargetInfo;break;case 4:detailPage=1;screen=Screen::TargetInfo;break;
      case 5:if(attackSelectAp(targetIndex)){screen=Screen::Attacks;menu=0;notify("AP copied to attack target");}else notify("Choose a live WiFi AP");break;
      case 6:if(attackSelectClient(targetIndex)){screen=Screen::Attacks;menu=0;notify("Client target selected");}else notify("Needs client of chosen AP");break;}}
    if(k==mp24::B)screen=Screen::Targets;return;
  }
  if(screen==Screen::TargetInfo){if(k==mp24::Left||k==mp24::Right)detailPage^=1;if(k==mp24::B)screen=Screen::TargetTools;return;}
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
    constexpr unsigned modes=unsigned(AttackMode::Count);moveMenu(k,modes+2);
    if(accept){if(unsigned(menu)<modes){pendingAttack=AttackMode(menu);screen=Screen::AttackConfirm;}else if(unsigned(menu)==modes){screen=Screen::Lab;menu=0;}else{attackStop();labStop();notify("Transmission stopped");}}
    if(k==mp24::B){attackStop();screen=Screen::Tools;menu=9;}return;
  }
  if(screen==Screen::AttackConfirm){
    if(k==mp24::Left)attackOptions.durationSeconds=attackOptions.durationSeconds<=5?60:attackOptions.durationSeconds-5;
    if(k==mp24::Right)attackOptions.durationSeconds=attackOptions.durationSeconds>=60?5:attackOptions.durationSeconds+5;
    if(k==mp24::D){auto& rate=attackOptions.packetsPerSecond;rate=rate<10?10:rate<25?25:rate<50?50:rate<100?100:1;}
    if(accept){if(phone.state().phonePriority())notify("Phone session has priority");else if(attackStart(pendingAttack))screen=Screen::AttackRun;else notify(attackStatus.message);}
    if(k==mp24::B){screen=Screen::Attacks;menu=unsigned(pendingAttack);}return;
  }
  if(screen==Screen::AttackRun){if(k==mp24::B||accept){attackStop();screen=Screen::Attacks;menu=unsigned(pendingAttack);}return;}
  if(screen==Screen::Lab){moveMenu(k,3);if(accept){if(menu==2)labStop();else{pendingLab=menu==0?LabMode::WifiBeacon:LabMode::BleBeacon;screen=Screen::LabConfirm;}}if(k==mp24::B){labStop();screen=Screen::Attacks;menu=0;}return;}
  if(screen==Screen::LabConfirm){if(accept){attackStop();auto& t=recon.targets[targetIndex];bool ok=labStart(pendingLab,channelLock?channelLock:1,t.advert,t.advertSize);screen=Screen::Lab;menu=2;notify(ok?"Transmission started":"Could not start test");}if(k==mp24::B)screen=Screen::Lab;return;}
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
  if(screen==Screen::Files){
    if(k==mp24::Down&&fileCount)fileIndex=(fileIndex+1)%fileCount;if(k==mp24::Up&&fileCount)fileIndex=(fileIndex+fileCount-1)%fileCount;
    if(k==mp24::Left&&filePage){--filePage;files();}if(k==mp24::Right&&fileMore){++filePage;files();}
    if(k==mp24::B){if(irBrowsing&&currentPath=="/hackerphone/ir"){screen=Screen::Infrared;menu=1;}else if(currentPath=="/"){screen=Screen::Home;menu=0;}else{int slash=currentPath.lastIndexOf('/');currentPath=slash<=0?"/":currentPath.substring(0,slash);filePage=0;files();}}
    if(accept&&fileCount){if(fileDirs[fileIndex]){currentPath=filePaths[fileIndex];filePage=0;files();}else if(irBrowsing&&fileNames[fileIndex].endsWith(".ir")){irOpen(filePaths[fileIndex].c_str());screen=Screen::IrSignal;}else{File file=SD.open(filePaths[fileIndex]);filePreview="";while(file&&file.available()&&filePreview.length()<168){char c=file.read();filePreview+=c>=32&&c<127?c:' ';}file.close();screen=Screen::FileView;}}return;
  }
  if(screen==Screen::FileView){if(k==mp24::B)screen=Screen::Files;return;}
  if(screen==Screen::Settings){
    moveMenu(k,6);
    if(accept){switch(menu){case 0:setTheme(themeIndex+1);break;case 1:if(radioRunning)radioStop();else startListening();break;
      case 2:radioSetMode(RadioMode((unsigned(radioMode)+1)%3));break;case 3:channelLock=(channelLock+1)%12;break;
      case 4:if(logging){session.flush();session.close();logging=false;}else if(!startLog())notify("SD log unavailable");break;case 5:phone.begin(now);break;}}
    if(k==mp24::B){screen=Screen::Home;menu=0;}return;
  }
  if(k==mp24::B)screen=Screen::Home;
}
static void render(uint32_t now){
  auto& f=mp24::frame;
  if(screen==Screen::Home){
    header("HACKERPHONE");f.setSwapBytes(true);f.pushImage(79,30,vtsWidth,vtsHeight,vtsPixels);
    static const char* names[]={"Phone","Messages","Security","Infrared","SD files","Settings","Hardware"};
    for(int row=0,first=menu/6*6;row<6&&first+row<7;++row){int i=first+row;if(i==menu)f.fillRoundRect(2,33+row*13,75,12,2,theme().selection);else f.fillRect(2,33+row*13,75,12,theme().background);text(5,35+row*13,names[i]);}
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
    header(screen==Screen::Hunt?"FOX HUNT / SIGNAL":targetFilter==1?"WI-FI ACCESS POINTS":targetFilter==2?"WI-FI STATIONS":targetFilter==3?"PROBE REQUESTS":targetFilter==4?"BLE DEVICES":"DEVICE DETAILS");auto& t=recon.targets[targetIndex];
    if(!targetVisible(t))text(5,40,"No matching observations");else{
      text(5,33,String(dragon::familyName(t.match.family)).substring(0,25),color(dragon::familyColor(t.match.family)));
      text(5,45,String(*t.name?t.name:"(unnamed)").substring(0,25));
      text(5,57,String(t.signal.value())+"dBm "+(t.fresh(now)?"LIVE":"STALE")+" CH"+String(t.channel),theme().accent);
      if(screen==Screen::Hunt){
        int previous=-1;for(unsigned i=0;i<48;++i){int v=t.history.recent(i,now);if(v==dragon::SignalHistory::Gap){previous=-1;continue;}int y=94-std::clamp(v+100,0,70)*26/70;if(previous>=0)f.drawLine(6+(i-1)*3,previous,6+i*3,y,theme().sweep);previous=y;}
        text(5,99,String(t.peak)+" peak / "+t.signal.direction(),theme().muted);
      }else{text(5,70,macText(t.mac),theme().muted);text(5,82,String(*vendorName?vendorName:addressKind(t.mac,t.ble,t.addressType)).substring(0,25),theme().muted);text(5,95,String(confidence(t.match.confidence))+" / "+t.packets+" frames",theme().muted);}
    }footer(screen==Screen::Hunt?"Signal only  B:Back":"Up/Dn A:Hunt D:More");
  }else if(screen==Screen::Files){header("SD FILE BROWSER");text(5,34,mp24::cardReady?currentPath.substring(0,19)+" p"+String(filePage+1):"SD unavailable");int first=(fileIndex/6)*6;for(int row=0;row<6&&first+row<fileCount;++row){int i=first+row;if(i==fileIndex)f.fillRect(1,47+row*11,158,11,theme().selection);text(4,48+row*11,String(fileDirs[i]?"+ ":"  ")+fileNames[i].substring(0,23));}footer("A:Open B:Up L/R:Page");}
  else if(screen==Screen::FileView){header("FILE PREVIEW");wrap(5,35,filePreview);footer("First 168 bytes / B:Back");}
  else if(screen==Screen::Settings){header("SETTINGS");String items[]={String("Theme: ")+theme().name,String("Radio: ")+(radioRunning?"running":"paused"),String("Scan: ")+modeName(),String("Channel: ")+(channelLock?String(channelLock):"hop 1-11"),String("CSV log: ")+(logging?"on":"off"),"Retry modem session"};menuRows(items,6);footer("A:Change  B:Back");}
  else if(screen==Screen::Tools){header("SECURITY TOOLKIT");String items[]={"RSSI radar","WiFi access points","WiFi stations","Probe requests","BLE devices","Channel analyzer","Traffic alerts",String(storageStatus.recording?"Stop & save PCAP":"Record WiFi PCAP"),"Record EAPOL only","WiFi / BLE attacks",String("Scan: ")+modeName(),"Stop all radio tools"};menuRows(items,12);footer("Up/Dn A:Open C:Home");}
  else if(screen==Screen::TargetTools){header("SELECTED DEVICE");String items[]={"Signal fox hunt","Connect / BLE GATT","Replay BLE advert","Detection evidence","Raw advertisement","Use as attack AP","Use as attack client"};menuRows(items,7);footer("A:Open B:Back");}
  else if(screen==Screen::TargetInfo){
    header(detailPage?"RAW BLE ADVERTISEMENT":"DETECTION EVIDENCE");auto& t=recon.targets[targetIndex];
    if(!detailPage){text(5,34,confidence(t.match.confidence),theme().accent);wrap(5,47,t.match.evidence,25,4);text(5,91,String("Age ")+(now-t.last)/1000+"s / peak "+t.peak+"dBm",theme().muted);text(5,103,t.signal.quality(),theme().muted);}
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
  else if(screen==Screen::Attacks){header("WIFI / BLE ATTACKS");constexpr unsigned count=unsigned(AttackMode::Count);String items[count+2];for(unsigned i=0;i<count;++i)items[i]=attackDefinitions[i].name;items[count]="Named beacons / replay";items[count+1]="Stop transmission";menuRows(items,count+2);footer("A:Review B:Back C:Home");}
  else if(screen==Screen::AttackConfirm){
    header("REVIEW ATTACK");const auto& def=attackDefinitions[unsigned(pendingAttack)];auto& t=attackOptions.target;
    text(5,34,String(def.name).substring(0,25),theme().accent);
    text(5,47,def.ble?"Nearby BLE receivers":String(t.ssid).substring(0,25));
    text(5,59,def.needsAp?macText(t.bssid):def.ble?"Pairing advertisement test":"Own radio / configured SSID",theme().muted);
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
  else if(screen==Screen::LabConfirm){header("REVIEW RADIO TEST");text(5,35,pendingLab==LabMode::WifiBeacon?"WiFi beacon transmission":pendingLab==LabMode::BleReplay?"BLE advert replay":"BLE beacon transmission",theme().accent);text(5,51,pendingLab==LabMode::BleReplay?"Selected payload; own MAC":"Name: HackerPhone-Lab");text(5,65,"Duration: 15 seconds");text(5,79,pendingLab==LabMode::WifiBeacon?"10 frames/s, selected CH":"100ms advertising interval");text(5,96,"C or B stops the session",theme().muted);footer("A:Start B:Cancel");}
  else if(screen==Screen::Infrared){header("INFRARED");String items[]={"Record remote signal","Browse SD remotes","Current signal",String("Carrier: ")+String(irFrequency()/1000)+" kHz","Save current signal","Stop IR"};menuRows(items,6);footer("A:Open B:Back C:Home");}
  else if(screen==Screen::IrSignal){
    header("IR REMOTE");const auto* s=irSignal();
    if(irStatus.state==IrState::Learning){text(5,38,"RECORDING",theme().accent);text(5,54,"Point remote at IR module");text(5,67,"Press a button once");text(5,80,String(irFrequency()/1000)+" kHz assumed carrier");}
    else{ text(5,35,s?String(s->name).substring(0,25):"No signal selected",theme().accent);text(5,49,s?String(s->count)+" pulses / "+String(s->frequency/1000)+" kHz":"Record or browse SD");text(5,63,irStatus.library?"SD button #"+String(irStatus.record):irStatus.unsaved?"UNSAVED - D saves to SD":"Stored signal");text(5,77,irStatus.state==IrState::Sending?"TRANSMITTING":"A:Send once  D:Save",irStatus.state==IrState::Sending?TFT_ORANGE:theme().muted);}
    wrap(5,91,irStatus.message,25,2);footer(irBusy()?"A/B:Stop C:Home":"L/R:Button B:IR menu");
  }
  else if(screen==Screen::Incoming){header("INCOMING CALL");wrap(5,42,phone.state().caller);text(5,80,"Audio not yet validated",TFT_ORANGE);footer("A:Answer B:Reject");}
  else {header("ENGINEERING BUILD 0.3");text(5,35,String("Keys ")+(mp24::keysReady?"OK":"FAIL")+" / "+mp24::keyEvents);text(5,47,String("SD ")+(mp24::cardReady?"OK":"unavailable"));text(5,59,"Heap "+String(ESP.getFreeHeap()/1024)+"K");text(5,71,"PSRAM "+String(ESP.getFreePsram()/1024)+"K");text(5,83,"Modem "+String(phone.state().online?"OK":"starting/offline"));text(5,95,"WiFi "+String(recon.wifiFrames)+" BLE "+String(recon.bleFrames));footer("C:Home");}
  if(notice.length()&&uint32_t(now-noticeAt)<2500){f.fillRect(0,101,160,14,theme().selection);text(3,104,notice.substring(0,26),TFT_YELLOW);}
  mp24::present();
}
static void serialCommands(uint32_t now){
  static char line[256];static unsigned n=0;static bool overflow=false;
  while(Serial.available()){char c=Serial.read();if(c=='\r')continue;if(c!='\n'){if(!overflow&&n+1<sizeof(line))line[n++]=c;else overflow=true;continue;}line[n]=0;n=0;if(overflow){overflow=false;Serial.println("HP_ERROR Command too long");continue;}
    if(!ready&&strcmp(line,"INFO")&&strcmp(line,"DISPLAY")){Serial.println("HP_NOT_READY Use INFO or DISPLAY");continue;}
    if(!strcmp(line,"INFO"))Serial.printf("HP_INFO v0.3.0-dev MP24 160x128 keys=%d events=%lu i2cerr=%lu sd=%d modem=%d sim=%u heap=%u psram=%u wifi=%lu ble=%lu drops=%lu\n",mp24::keysReady,(unsigned long)mp24::keyEvents,(unsigned long)mp24::i2cErrors,mp24::cardReady,phone.state().online,unsigned(phone.state().sim),ESP.getFreeHeap(),ESP.getFreePsram(),(unsigned long)recon.wifiFrames,(unsigned long)recon.bleFrames,(unsigned long)radioDrops);
    else if(!strcmp(line,"DISPLAY"))Serial.printf("HP_DISPLAY frames=%lu errors=%lu light=%d\n",(unsigned long)mp24::displayFrames,(unsigned long)mp24::displayErrors,mp24::lightReady);
    else if(!strcmp(line,"LIGHT")){mp24::backlight(true);Serial.printf("HP_LIGHT ready=%u\n",mp24::lightReady);}
    else if(!strcmp(line,"HEALTH"))Serial.printf("HP_HEALTH uptime=%lu stack_free=%u heap_ok=%d logging=%d log_bytes=%lu targets=%u\n",(unsigned long)now,unsigned(uxTaskGetStackHighWaterMark(nullptr)),heap_caps_check_integrity_all(false),logging,(unsigned long)(session?session.size():0),unsigned(recon.count(now)));
    else if(!strcmp(line,"HOME")){irStop();attackStop();labStop();gattStop();screen=Screen::Home;menu=0;recon.selected=recon.pinned=-1;}
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
    else if(!strcmp(line,"ATTACKS")){screen=Screen::Attacks;menu=0;}
    else if(!strcmp(line,"ATTACK LIST")){for(unsigned i=0;i<unsigned(AttackMode::Count);++i)Serial.printf("HP_ATTACK_MODE %u %s\n",i,attackDefinitions[i].name);Serial.println("HP_ATTACK_END");}
    else if(!strcmp(line,"ATTACK STATUS")){
      auto& t=attackOptions.target;Serial.printf("HP_ATTACK active=%u mode=%u accepted=%lu errors=%lu driver=%d adapter=%u message=%s\n",attackStatus.active,unsigned(attackStatus.mode),(unsigned long)attackStatus.accepted,(unsigned long)attackStatus.errors,attackStatus.lastError,rawFrameAdapterLinked(),attackStatus.message);
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
    else if(!strncmp(line,"MODE ",5)&&strlen(line)==6&&line[5]>='0'&&line[5]<='2'){if(attackStatus.active||gattStatus().busy||gattStatus().connected||lab.mode!=LabMode::Off)Serial.println("HP_ERROR Stop active tool first");else{radioSetMode(RadioMode(line[5]-'0'));Serial.println("HP_MODE_OK");}}
    else if(!strcmp(line,"PCAP START")||!strcmp(line,"PCAP EAPOL")){bool ok=startListening()&&captureStart(!strcmp(line,"PCAP EAPOL"));Serial.printf("HP_PCAP %s %s\n",ok?"STARTED":"FAILED",ok?storageStatus.path:"");}
    else if(!strcmp(line,"PCAP STOP")){captureStop();Serial.printf("HP_PCAP SAVED %s bytes=%lu records=%lu drops=%lu error=%u\n",storageStatus.path,(unsigned long)storageStatus.bytes,(unsigned long)storageStatus.records,(unsigned long)storageStatus.drops,storageStatus.error);}
    else if(!strcmp(line,"TARGETS")){for(unsigned i=0;i<ReconCore::Capacity;++i){auto& t=recon.targets[i];if(t.used)Serial.printf("HP_TARGET %u %s %s type=%u rssi=%d family=%s name=%s live=%u age_ms=%lu\n",i,t.ble?"BLE":"WiFi",macText(t.mac).c_str(),t.addressType,t.signal.value(),dragon::familyName(t.match.family),t.name,t.fresh(now),(unsigned long)(now-t.last));}Serial.println("HP_TARGETS_END");}
    else if(!strncmp(line,"BLEAD ",6)){char* end;unsigned long i=strtoul(line+6,&end,10);if(end==line+6||*end||i>=ReconCore::Capacity||!recon.targets[i].used||!recon.targets[i].ble)Serial.println("HP_ERROR BLE target index");else{auto& t=recon.targets[i];Serial.printf("HP_AD %lu ",i);for(unsigned j=0;j<t.advertSize;++j)Serial.printf("%02X",t.advert[j]);Serial.println();}}
    else if(!strcmp(line,"GATT STATUS")){auto g=gattStatus();Serial.printf("HP_GATT connected=%u busy=%u rows=%u truncated=%u notifications=%lu message=%s\n",g.connected,g.busy,g.rows,g.truncated,(unsigned long)g.notifications,g.message);Serial.printf("HP_VALUE handle=%04X hex=",g.valueHandle);for(unsigned i=0;i<g.valueSize;++i)Serial.printf("%02X",g.value[i]);Serial.println();}
    else if(!strcmp(line,"GATT DUMP")){GattRow row;for(unsigned i=0;gattRow(i,row);++i)Serial.printf("HP_GATT_ROW %u %04X %s %s flags=%u\n",i,row.handle,row.service,row.uuid,row.flags);Serial.println("HP_GATT_END");}
    else if(!strcmp(line,"GATT STOP")){gattStop();Serial.println("HP_GATT_STOP");}
    else if(!strncmp(line,"GATT CONNECT ",13)){char* end;unsigned long i=strtoul(line+13,&end,10);bool ok=end!=line+13&&!*end&&i<ReconCore::Capacity&&recon.targets[i].ble&&recon.targets[i].fresh(now)&&!attackStatus.active&&lab.mode==LabMode::Off&&!phone.state().phonePriority()&&gattConnect(recon.targets[i].mac,recon.targets[i].addressType);Serial.printf("HP_GATT %s\n",ok?"QUEUED":"FAILED");}
    else if(!strncmp(line,"GATT READ ",10)||!strncmp(line,"GATT SUB ",9)||!strncmp(line,"GATT UNSUB ",11)){
      unsigned offset=!strncmp(line,"GATT READ ",10)?10:!strncmp(line,"GATT SUB ",9)?9:11;char* end;unsigned long handle=strtoul(line+offset,&end,16);bool ok=end!=line+offset&&!*end&&handle&&handle<=65535&&!phone.state().phonePriority();if(ok)ok=offset==10?gattRead(handle):gattSubscribe(handle,offset==9);Serial.printf("HP_GATT %s\n",ok?"QUEUED":"FAILED");}
    else if(!strncmp(line,"GATT WRITE ",11)){
      char* end;unsigned long handle=strtoul(line+11,&end,16);uint8_t bytes[64];size_t size=0;bool ok=end!=line+11&&*end==' '&&handle&&handle<=65535&&!phone.state().phonePriority();
      if(ok){const char* hex=end+1;size_t length=strlen(hex);ok=length&&length%2==0&&length<=128;for(size_t i=0;ok&&i<length;i+=2){auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;};int a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0)ok=false;else bytes[size++]=(a<<4)|b;}}
      if(ok)ok=gattWrite(handle,bytes,size,true);Serial.printf("HP_GATT %s\n",ok?"QUEUED":"FAILED");}
    else if(!strncmp(line,"FILEINFO ",9)){
      const char* relative=line+9;if(!safeRelativePath(relative))Serial.println("HP_FILE_ERROR path");else{File f=SD.open(String("/hackerphone/")+relative,FILE_READ);if(!f||f.isDirectory())Serial.println("HP_FILE_ERROR missing");else Serial.printf("HP_FILEINFO %lu\n",(unsigned long)f.size());f.close();}}
    else if(!strncmp(line,"FILECHUNK ",10)){
      char* separator=strchr(line+10,' ');if(!separator){Serial.println("HP_FILE_ERROR syntax");continue;}*separator=0;char* end;unsigned long offset=strtoul(separator+1,&end,10);
      if(end==separator+1||*end||!safeRelativePath(line+10)){Serial.println("HP_FILE_ERROR path/offset");continue;}
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
  preferences.begin("hackerphone",false);themeIndex=preferences.getUChar("theme",0)%ThemeCount;
  Serial.begin(115200);Serial.setTxTimeoutMs(20);Serial.setDebugOutput(true);
  delay(250);Serial.println("HP_STAGE usb");
  ready=mp24::begin();
  if(!ready){Serial.println("HP_FAIL framebuffer");return;}
  irBegin();
  auto& f=mp24::frame;f.fillSprite(TFT_BLACK);f.setSwapBytes(true);f.pushImage((160-bwmWidth)/2,7,bwmWidth,bwmHeight,bwmPixels);text(44,91,"HACKERPHONE");text(19,107,"Engineering preview",theme().muted);mp24::present();
  storageBegin();mp24::modemBegin();delay(1200);Serial.println("HP_READY MP24 engineering preview 0.3.0-dev");
}
void loop(){if(!ready){serialCommands(millis());delay(10);return;}uint32_t now=millis();mp24::modemPoll(phone,now);serialCommands(now);now=millis();key(mp24::key(now),now);now=millis();
  if(phone.state().phonePriority()){irStop();attackStop();if(radioRunning)radioStop();if(lab.mode!=LabMode::Off)labStop();auto gs=gattStatus();if(gs.busy||gs.connected)gattStop();if(phone.state().call==Call::Ringing)screen=Screen::Incoming;}
  radioTick(now);labTick(now);attackTick(now);irTick(now);storageTick(now);saveTargets(now);
  if(uint32_t(now-lastDraw)>=100){lastDraw=now;render(now);}delay(2);
}
