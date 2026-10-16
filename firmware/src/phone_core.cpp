#include "phone_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace hackerphone {
namespace {
void copy(char* dst,size_t cap,const char* src) { if(cap){snprintf(dst,cap,"%s",src?src:"");} }
bool prefix(const char* text,const char* p) { return strncmp(text,p,strlen(p))==0; }
bool number(const char* p,int& result,const char** end=nullptr) {
  while(*p==' ')++p;
  if(*p<'0'||*p>'9')return false;
  unsigned n=0;
  while(*p>='0'&&*p<='9') { n=n*10+unsigned(*p++-'0');if(n>65535)return false; }
  result=int(n);if(end)*end=p;return true;
}
}

bool validNumber(const char* p) {
  if(!p)return false;
  if(*p=='+')++p;
  unsigned n=0;
  for(;*p;++p) { if(*p<'0'||*p>'9'||++n>20)return false; }
  return n>0;
}

// UCS-2 PDU keeps quotes, newlines and Ctrl-Z out of the AT command stream.
// A single message supports 70 BMP codepoints. No truncation or silent splitting.
Request encodeSms(const char* destination,const char* utf8,SmsPdu& out) {
  out={};
  if(!validNumber(destination)||!utf8||!*utf8)return Request::Invalid;
  uint8_t text[140]{};size_t used=0;
  const auto* p=reinterpret_cast<const uint8_t*>(utf8);
  while(*p) {
    uint32_t cp=0;unsigned extra=0;uint32_t minimum=0;
    uint8_t lead=*p++;
    if(lead<0x80)cp=lead;
    else if(lead>=0xc2&&lead<=0xdf){cp=lead&31;extra=1;minimum=0x80;}
    else if(lead>=0xe0&&lead<=0xef){cp=lead&15;extra=2;minimum=0x800;}
    else if(lead>=0xf0&&lead<=0xf4){cp=lead&7;extra=3;minimum=0x10000;}
    else return Request::UnsupportedText;
    for(unsigned i=0;i<extra;++i){if((*p&0xc0)!=0x80)return Request::UnsupportedText;cp=(cp<<6)|(*p++&63);}
    if(cp<minimum||cp>0xffff||(cp>=0xd800&&cp<=0xdfff))return Request::UnsupportedText;
    if(used+2>sizeof(text))return Request::TooLong;
    text[used++]=uint8_t(cp>>8);text[used++]=uint8_t(cp);
  }
  uint8_t bytes[166]{};size_t n=0;
  const bool international=destination[0]=='+';
  const char* digits=destination+(international?1:0);
  size_t len=strlen(digits);
  bytes[n++]=0; // Use the SIM's configured SMS service centre.
  bytes[n++]=1; // SMS-SUBMIT, no validity period or status-report request.
  bytes[n++]=0;bytes[n++]=uint8_t(len);bytes[n++]=international?0x91:0x81;
  for(size_t i=0;i<len;i+=2)bytes[n++]=uint8_t((digits[i]-'0')|((i+1<len?digits[i+1]-'0':15)<<4));
  bytes[n++]=0;bytes[n++]=8;bytes[n++]=uint8_t(used);
  memcpy(bytes+n,text,used);n+=used;
  const char* hex="0123456789ABCDEF";
  for(size_t i=0;i<n;++i){out.hex[2*i]=hex[bytes[i]>>4];out.hex[2*i+1]=hex[bytes[i]&15];}
  out.hex[n*2]=0;out.tpduBytes=uint16_t(n-1);out.characters=uint8_t(used/2);
  return Request::Accepted;
}

bool PhoneCore::busy() const { return suspended_||active_!=Cmd::None||settling_||initStep_<5; }
void PhoneCore::error(const char* text){copy(state_.error,sizeof(state_.error),text);}
void PhoneCore::begin(uint32_t now) {
  auto previousSms=state_.smsOutcome;state_={};state_.smsOutcome=previousSms==Outcome::Pending?Outcome::Uncertain:previousSms;
  active_=Cmd::None;started_=true;initStep_=0;pollStep_=0;recoverAllowed_=true;
  lineSize_=0;discard_=false;settling_=false;pollAt_=now;tick(now);
}
bool PhoneCore::command(Cmd kind,const char* text,uint32_t now,uint32_t timeout) {
  if(active_!=Cmd::None||settling_)return false;
  active_=kind;sentAt_=now;timeoutMs_=timeout;
  char buffer[80];int len=snprintf(buffer,sizeof(buffer),"%s\r",text);
  if(len<0||size_t(len)>=sizeof(buffer)||!transport_.write(buffer,size_t(len))){finish(false,now,"UART write failed");return false;}
  return true;
}
Request PhoneCore::inspect(const char* text,uint32_t now){
  static const char* queries[]={"AT","ATI","AT+CGMR","AT+CFUN?","AT+CPIN?","AT+CEREG?","AT+CREG?","AT+CGREG?","AT+CSQ","AT+COPS?","AT+CGATT?","AT+CGDCONT?","AT+QNWINFO","AT+QENG=\"servingcell\"","AT+QCFG=\"nwscanmode\"","AT+QCFG=\"band\"","AT+QCFG=\"ims\"","AT+QMBNCFG=\"List\"","AT+QIACT?","AT+QGPS?","AT+QGPSLOC?","AT+CCLK?","AT+QDAI?","AT+QDAI=?","AT+QAUDMOD?","AT+CEER","AT+CIREG?","AT+QGPSLOC=2"};
  bool allowed=false;for(const char* query:queries)if(text&&!strcmp(text,query)){allowed=true;break;}
  if(!allowed)return Request::Invalid;
  return diagnosticCommand(text,now);
}
Request PhoneCore::configure(const char* setting,uint32_t now){
  if(!setting)return Request::Invalid;
  struct Setting {const char* name;const char* command;};
  static const Setting settings[]={
    {"AUTO","AT+QCFG=\"nwscanmode\",0,1"},
    {"LTE","AT+QCFG=\"nwscanmode\",3,1"},
    {"MINT","AT+CGDCONT=1,\"IPV4V6\",\"Wholesale\""},
    {"IMS_ON","AT+QCFG=\"ims\",1"},
    {"IMS_DEFAULT","AT+QCFG=\"ims\",0"},
    {"RESTART","AT+CFUN=1,1"},
    {"GPS_ON","AT+QGPS=1"},
    {"GPS_OFF","AT+QGPSEND"},
    {"PCM256","AT+QDAI=1,1,0,1,0,0,1,1"},
    {"PCM2048","AT+QDAI=1,1,0,4,0,0,1,1"},
    {"PCM_TONE","AT+QLDTMF=10,\"1\""},
    {"PCM1024","AT+QDAI=1,1,0,3,0,0,1,1"},
    {"PCM16K","AT+QDAI=1,1,0,4,0,1,1,1"}
  };
  for(const auto& s:settings)if(!strcmp(setting,s.name))return diagnosticCommand(s.command,now,15000);
  return Request::Invalid;
}
Request PhoneCore::diagnosticCommand(const char* text,uint32_t now,uint32_t timeout){
  if(busy()||state_.phonePriority())return Request::Busy;
  diagnostic_={};copy(diagnostic_.command,sizeof(diagnostic_.command),text);diagnostic_.outcome=Outcome::Pending;
  return command(Cmd::Diagnostic,text,now,timeout)?Request::Accepted:Request::Invalid;
}
void PhoneCore::tick(uint32_t now) {
  if(!started_||suspended_)return;
  if(active_!=Cmd::None) {
    if(uint32_t(now-sentAt_)>=timeoutMs_){++state_.timeouts;abort(now,"Modem response timeout",active_==Cmd::SmsBody);}
    return;
  }
  if(settling_) {
    // Recover with a tagged read-only response. Never replay a call or SMS,
    // and do not let an old bare OK acknowledge the new session.
    if(recoverAllowed_&&uint32_t(now-settleAt_)>=5000){settling_=false;resyncSeen_=false;command(Cmd::Resync,"AT+CMEE?",now,2000);}
    return;
  }
  if(initStep_<5) {
    static const Cmd kinds[]={Cmd::Probe,Cmd::Echo,Cmd::Errors,Cmd::Clip,Cmd::SmsNotice};
    static const char* texts[]={"AT","ATE0","AT+CMEE=2","AT+CLIP=1","AT+CNMI=2,1,0,0,0"};
    command(kinds[initStep_],texts[initStep_],now);return;
  }
  if(uint32_t(now-pollAt_)< (state_.call!=Call::Idle?1000u:state_.sim==Sim::Ready?5000u:30000u))return;
  if(state_.call!=Call::Idle){callSeen_=false;command(Cmd::Calls,"AT+CLCC",now);}
  else if(state_.sim!=Sim::Ready||pollStep_==0){pollStep_=1;command(Cmd::SimQuery,"AT+CPIN?",now);}
  else if(pollStep_==1){pollStep_=2;command(Cmd::RegQuery,"AT+CEREG?",now);}
  else {pollStep_=0;command(Cmd::Signal,"AT+CSQ",now);}
  pollAt_=now;
}
Request PhoneCore::available() const {
  if(busy())return Request::Busy;
  if(state_.sim!=Sim::Ready)return Request::NoSim;
  if(!state_.registered()||!state_.online)return Request::NoNetwork;
  return Request::Accepted;
}
Request PhoneCore::dial(const char* number,uint32_t now) {
  if(!validNumber(number))return Request::Invalid;
  Request result=available();if(result!=Request::Accepted)return result;
  if(state_.call!=Call::Idle)return Request::Busy;
  char text[32];snprintf(text,sizeof(text),"ATD%s;",number);
  state_.callOutcome=Outcome::Pending;state_.call=Call::Dialing;error("");
  command(Cmd::Dial,text,now,30000);return Request::Accepted;
}
Request PhoneCore::answer(uint32_t now) {
  if(busy())return Request::Busy;
  if(state_.call!=Call::Ringing)return Request::Invalid;
  state_.callOutcome=Outcome::Pending;command(Cmd::Answer,"ATA",now,30000);return Request::Accepted;
}
Request PhoneCore::hangup(uint32_t now) {
  if(busy())return Request::Busy;
  if(state_.call==Call::Idle)return Request::Invalid;
  state_.call=Call::Ending;state_.callOutcome=Outcome::Pending;
  command(Cmd::Hangup,"AT+CHUP",now);return Request::Accepted;
}
Request PhoneCore::sendSms(const char* number,const char* text,uint32_t now) {
  Request r=available();if(r!=Request::Accepted)return r;
  if(state_.call!=Call::Idle)return Request::Busy;
  r=encodeSms(number,text,pdu_);if(r!=Request::Accepted)return r;
  state_.smsOutcome=Outcome::Pending;smsAck_=false;error("");
  command(Cmd::SmsMode,"AT+CMGF=0",now);return Request::Accepted;
}
void PhoneCore::abort(uint32_t now,const char* reason,bool uncertain) {
  Cmd previous=active_;
  if(previous==Cmd::SmsPrompt){const char escape=27;transport_.write(&escape,1);}
  finish(false,now,reason);
  if(previous==Cmd::Diagnostic)diagnostic_.outcome=Outcome::TimedOut;
  if(previous==Cmd::SmsBody||previous==Cmd::SmsPrompt||previous==Cmd::SmsMode)
    state_.smsOutcome=uncertain?Outcome::Uncertain:Outcome::Failed;
  settling_=true;settleAt_=now;state_.online=false;
}
void PhoneCore::finish(bool success,uint32_t now,const char* reason) {
  Cmd completed=active_;active_=Cmd::None;
  if(completed==Cmd::Resync){if(success){state_.online=true;initStep_=1;pollAt_=now;error("");}else{settling_=true;settleAt_=now;}return;}
  if(completed==Cmd::Diagnostic)diagnostic_.outcome=success?Outcome::Accepted:Outcome::Failed;
  if(reason)error(reason);
  if(completed==Cmd::Probe)state_.online=success;
  if(completed>=Cmd::Probe&&completed<=Cmd::SmsNotice) {
    if(!success&&completed==Cmd::Probe){settling_=true;settleAt_=now;return;}
    ++initStep_;
    if(initStep_==5){pollAt_=now-30000u;}
  }
  if(completed==Cmd::Dial||completed==Cmd::Answer||completed==Cmd::Hangup) {
    state_.callOutcome=success?Outcome::Accepted:Outcome::Failed;
    if(!success||completed==Cmd::Hangup){state_.call=Call::Idle;state_.caller[0]=0;}
    // ATA/ATD OK only acknowledges the command; CLCC confirms an active call.
    pollAt_=now-1000u;
  }
  if(completed==Cmd::Calls&&success&&!callSeen_){state_.call=Call::Idle;state_.caller[0]=0;}
  if(completed==Cmd::SmsMode) {
    if(success){char cmd[32];snprintf(cmd,sizeof(cmd),"AT+CMGS=%u",pdu_.tpduBytes);command(Cmd::SmsPrompt,cmd,now,10000);}
    else state_.smsOutcome=Outcome::Failed;
  } else if(completed==Cmd::SmsPrompt)state_.smsOutcome=Outcome::Failed;
  else if(completed==Cmd::SmsBody){state_.smsOutcome=success&&smsAck_?Outcome::Accepted:success?Outcome::Uncertain:Outcome::Failed;pdu_={};}
}
void PhoneCore::feed(const uint8_t* data,size_t length,uint32_t now) {
  if(!data)return;
  for(size_t i=0;i<length;++i) {
    const uint8_t c=data[i];
    if(c=='>'&&active_==Cmd::SmsPrompt&&lineSize_==0&&!discard_) {
      active_=Cmd::SmsBody;sentAt_=now;timeoutMs_=120000;
      const char end=26;
      if(!transport_.write(pdu_.hex,strlen(pdu_.hex))||!transport_.write(&end,1))abort(now,"SMS UART write failed",true);
      continue;
    }
    if(c=='\r')continue;
    if(c=='\n'){
      if(!discard_&&lineSize_){line_[lineSize_]=0;line(line_,now);}
      discard_=false;lineSize_=0;continue;
    }
    if(c==' '&&lineSize_==0)continue;
    if(discard_)continue;
    if(c<32||c>126||lineSize_+1>=sizeof(line_)){discard_=true;lineSize_=0;++state_.malformedLines;continue;}
    line_[lineSize_++]=char(c);
  }
}
void PhoneCore::line(const char* text,uint32_t now) {
  if(active_==Cmd::Diagnostic){size_t n=strlen(diagnostic_.reply),length=strlen(text);if(n+length+2<sizeof(diagnostic_.reply)){memcpy(diagnostic_.reply+n,text,length);diagnostic_.reply[n+length]='\n';diagnostic_.reply[n+length+1]=0;}else diagnostic_.truncated=true;}
  if(!strcmp(text,"RDY")||!strcmp(text,"POWERED DOWN")) {
    if(active_!=Cmd::None)abort(now,"Modem restarted",active_==Cmd::SmsBody);
    state_.online=false;state_.sim=Sim::Unknown;state_.registration=state_.circuitRegistration=state_.packetRegistration=0;state_.call=Call::Idle;
    state_.caller[0]=0;settling_=true;settleAt_=now;recoverAllowed_=!strcmp(text,"RDY");return;
  }
  if(!strcmp(text,"RING")){state_.call=Call::Ringing;return;}
  if(!strcmp(text,"NO CARRIER")||!strcmp(text,"BUSY")||!strcmp(text,"NO ANSWER")){
    state_.call=Call::Idle;state_.caller[0]=0;
    if(active_==Cmd::Dial||active_==Cmd::Answer||active_==Cmd::Hangup)finish(false,now,text);
    return;
  }
  if(prefix(text,"+CLIP: \"")) {
    const char* p=text+8;const char* end=strchr(p,'"');
    if(end&&end-p>0&&size_t(end-p)<sizeof(state_.caller)){
      char caller[24]{};memcpy(caller,p,size_t(end-p));if(validNumber(caller))copy(state_.caller,sizeof(state_.caller),caller);
    }return;
  }
  if(prefix(text,"+CPIN: ")) {
    const char* value=text+7;
    state_.sim=!strcmp(value,"READY")?Sim::Ready:!strcmp(value,"SIM PIN")?Sim::PinRequired:!strcmp(value,"SIM PUK")?Sim::PukRequired:!strcmp(value,"NOT INSERTED")?Sim::Absent:Sim::Unknown;
    if(state_.sim!=Sim::Ready){
      state_.registration=0;
      if(active_==Cmd::SmsMode||active_==Cmd::SmsPrompt||active_==Cmd::SmsBody)
        abort(now,"SIM no longer ready",active_==Cmd::SmsBody);
    }
    return;
  }
  if(prefix(text,"+CEREG:")||prefix(text,"+CREG:")||prefix(text,"+CGREG:")) {
    const char* p=strchr(text,':')+1;int first=0,second=0;const char* end=nullptr;
    if(number(p,first,&end)){
      while(*end==' ')++end;
      // Query response <n>,<stat>; URC <stat>,"tac",... has a quoted second item.
      if(*end==','&&number(end+1,second))first=second;
      if(first<=10){if(prefix(text,"+CEREG:"))state_.registration=uint8_t(first);else if(prefix(text,"+CGREG:"))state_.packetRegistration=uint8_t(first);else state_.circuitRegistration=uint8_t(first);}
    }return;
  }
  if(prefix(text,"+CSQ:")){int r=99;if(number(text+5,r)){state_.signalKnown=r<=31;state_.signalDbm=state_.signalKnown?int16_t(-113+2*r):-127;}return;}
  if(prefix(text,"+CLCC:")) {
    int id=0,dir=0,status=0,mode=0,multi=0;
    if(sscanf(text+6," %d,%d,%d,%d,%d",&id,&dir,&status,&mode,&multi)==5&&id>0&&mode==0&&status>=0&&status<=5){
      const Call states[]={Call::Active,Call::Held,Call::Dialing,Call::Alerting,Call::Ringing,Call::Ringing};
      state_.call=states[status];callSeen_=true;
      const char* quote=strchr(text,'"');if(quote){const char* end=strchr(quote+1,'"');if(end&&end-quote-1>0&&size_t(end-quote-1)<sizeof(state_.caller)){char caller[24]{};memcpy(caller,quote+1,end-quote-1);if(validNumber(caller))copy(state_.caller,sizeof(state_.caller),caller);}}
    }return;
  }
  if(prefix(text,"+CMTI:")) {
    char storage[4]{};int index=-1;
    if(sscanf(text+6," \"%3[A-Z]\",%d",storage,&index)==2&&index>=0&&index<=32767){
      ++state_.inboxNotifications;state_.newestSmsIndex=int16_t(index);copy(state_.newestSmsStorage,sizeof(state_.newestSmsStorage),storage);
    }return;
  }
  if(prefix(text,"+CMGS:")){int reference=0;if(active_==Cmd::SmsBody&&number(text+6,reference))smsAck_=true;return;}
  if(prefix(text,"+CME ERROR:")) {
    if(strstr(text,"SIM not inserted")||!strcmp(text,"+CME ERROR: 10")){state_.sim=Sim::Absent;state_.registration=0;}
    else if(strstr(text,"SIM PIN required")||!strcmp(text,"+CME ERROR: 11"))state_.sim=Sim::PinRequired;
  }
  if(!strcmp(text,"ERROR")||prefix(text,"+CME ERROR:")||prefix(text,"+CMS ERROR:")){
    if(active_!=Cmd::None)finish(false,now,text);return;
  }
  if(prefix(text,"+CMEE:")&&active_==Cmd::Resync){int mode;if(number(text+6,mode)&&mode<=2)resyncSeen_=true;return;}
  if(!strcmp(text,"OK")&&active_!=Cmd::None&&(active_!=Cmd::Resync||resyncSeen_))finish(true,now);
}
}
