#include "band_core.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

namespace hackerband {
void put32(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static bool known(Kind kind){return unsigned(kind)>=unsigned(Kind::Time)&&unsigned(kind)<=unsigned(Kind::Result);}
size_t encode(const Packet& p,uint8_t* out,size_t cap){
  if(!out||!known(p.kind)||!p.session||!p.sequence||p.size>MaxPayload||cap<HeaderSize+p.size)return 0;
  out[0]='H';out[1]='B';out[2]=ProtocolVersion;out[3]=uint8_t(p.kind);
  put32(out+4,p.session);put32(out+8,p.sequence);out[12]=uint8_t(p.size);out[13]=uint8_t(p.size>>8);
  memcpy(out+HeaderSize,p.payload,p.size);return HeaderSize+p.size;
}
bool decode(const uint8_t* b,size_t n,Packet& out){
  out={};
  if(!b||n<HeaderSize||b[0]!='H'||b[1]!='B'||b[2]!=ProtocolVersion||!known(Kind(b[3])))return false;
  uint16_t size=uint16_t(b[12])|(uint16_t(b[13])<<8);
  if(size>MaxPayload||n!=HeaderSize+size||!get32(b+4)||!get32(b+8))return false;
  out.kind=Kind(b[3]);out.session=get32(b+4);out.sequence=get32(b+8);out.size=size;
  memcpy(out.payload,b+HeaderSize,size);return true;
}
bool Session::open(uint32_t id,bool encrypted,bool authenticated){close();if(!id||!encrypted||!authenticated)return false;id_=id;return true;}
void Session::close(){id_=received_=sent_=0;}
bool Session::accept(const Packet& p){
  if(!id_||p.session!=id_||!known(p.kind)||p.size>MaxPayload||p.sequence<=received_)return false;
  received_=p.sequence;return true;
}
bool Session::make(Kind kind,const uint8_t* payload,size_t size,Packet& p){
  p={};if(!id_||sent_==UINT32_MAX||!known(kind)||size>MaxPayload||(size&&!payload))return false;
  p.kind=kind;p.session=id_;p.sequence=++sent_;p.size=uint16_t(size);if(size)memcpy(p.payload,payload,size);return true;
}
void Stopwatch::tick(uint32_t now){if(running_)elapsed_+=uint32_t(now-at_);at_=now;}
void Stopwatch::toggle(uint32_t now){tick(now);running_=!running_;}
void Stopwatch::reset(uint32_t now){elapsed_=0;at_=now;running_=false;}
uint64_t Stopwatch::elapsed(uint32_t now)const{return elapsed_+(running_?uint32_t(now-at_):0);}
bool Clock::set(uint32_t utc,int16_t offsetMinutes,uint32_t now){
  if(utc<1577836800u||utc>4102444799u||offsetMinutes< -720||offsetMinutes>840)return false;
  utcMs_=uint64_t(utc)*1000;offset_=offsetMinutes;at_=now;valid_=true;return true;
}
void Clock::tick(uint32_t now){if(valid_)utcMs_+=uint32_t(now-at_);at_=now;}
uint64_t Clock::utc(uint32_t now)const{return valid_?(utcMs_+uint32_t(now-at_))/1000:0;}
int64_t Clock::local(uint32_t now)const{return valid_?int64_t(utc(now))+int64_t(offset_)*60:0;}
static void clean(char* out,size_t capacity,const char* in){
  size_t n=0;if(in)while(*in&&n+1<capacity){unsigned char c=*in++;out[n++]=(c>=32&&c<127)?char(c):' ';}out[n]=0;
}
bool IncomingCall::ring(uint32_t id,const char* caller,uint32_t now){
  if(!id)return false;
  if(id_!=id){ignored_=false;declinePending_=false;}
  id_=id;refreshed_=now;clean(caller_,sizeof(caller_),caller);return true;
}
void IncomingCall::end(uint32_t id){if(id==id_)disconnect();}
void IncomingCall::disconnect(){id_=0;ignored_=declinePending_=false;caller_[0]=0;}
void IncomingCall::tick(uint32_t now){if(active()&&uint32_t(now-refreshed_)>=15000)disconnect();}
CallAction IncomingCall::ignore(){if(!active()||ignored_)return CallAction::None;ignored_=true;return CallAction::Ignore;}
CallAction IncomingCall::decline(){if(!active()||declinePending_)return CallAction::None;declinePending_=true;return CallAction::Decline;}
void IncomingCall::declineResult(bool accepted){if(!accepted)declinePending_=false;}
bool canDecline(uint32_t requested,uint32_t current,bool ringing,bool authenticated){return authenticated&&ringing&&current&&requested==current;}
bool Inbox::push(uint32_t id,const char* title,const char* body){
  if(!id||!title||!body)return false;
  for(size_t i=0;i<count_;++i)if(items_[i].id==id)return false;
  auto& item=items_[next_];item={};item.id=id;clean(item.title,sizeof(item.title),title);clean(item.body,sizeof(item.body),body);
  next_=(next_+1)%Capacity;if(count_<Capacity)++count_;return true;
}
const Notice* Inbox::latest()const{return count_?&items_[(next_+Capacity-1)%Capacity]:nullptr;}
void Inbox::clear(){for(auto& item:items_)item={};next_=count_=0;}
void AlertGate::reset(){for(auto& e:entries_)e={};alertAt_=0;alerted_=false;}
bool AlertGate::observe(uint32_t id,uint8_t confidence,uint32_t now){
  if(!id||confidence<2||confidence>3)return false;
  Entry* entry=nullptr;Entry* oldest=&entries_[0];
  for(auto& e:entries_){if(e.id==id){entry=&e;break;}if(!e.id||(oldest->id&&uint32_t(now-e.last)>uint32_t(now-oldest->last)))oldest=&e;}
  if(!entry){entry=oldest;*entry={id,now,now,1};return false;}
  if(uint32_t(now-entry->last)>10000){*entry={id,now,now,1};return false;}
  if(uint32_t(now-entry->last)<1000)return false;
  entry->last=now;if(entry->hits<255)++entry->hits;
  if(entry->hits<3||uint32_t(now-entry->first)<2000||(alerted_&&uint32_t(now-alertAt_)<60000))return false;
  alerted_=true;alertAt_=now;return true;
}
uint32_t MatrixRain::next(){random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return random_;}
bool MatrixRain::begin(uint8_t columns,uint8_t rows,uint32_t seed,uint32_t now){
  columns_=rows_=0;
  if(!columns||columns>MaxColumns||rows<4||rows>64)return false;
  columns_=columns;rows_=rows;random_=seed?seed:1;frame_=0;at_=now;interacting_=false;
  for(unsigned i=0;i<columns_;++i){heads_[i]=next()%rows_;lengths_[i]=3+next()%(rows_-2);}
  return true;
}
void MatrixRain::interact(uint32_t now){interactionAt_=now;interacting_=true;}
void MatrixRain::tick(uint32_t now){
  if(!columns_)return;
  if(interacting_&&uint32_t(now-interactionAt_)>=1500)interacting_=false;
  const uint32_t step=interacting_?60:120;
  uint32_t frames=uint32_t(now-at_)/step;
  if(!frames)return;
  // Do not spin through missed frames after sleep or a paused screen.
  at_=now;if(frames>4)frames=4;
  while(frames--){++frame_;for(unsigned i=0;i<columns_;++i)
    if(interacting_||(frame_+i)%3)heads_[i]=(heads_[i]+1)%rows_;}
}
RainCell MatrixRain::cell(uint8_t column,uint8_t row)const{
  if(column>=columns_||row>=rows_)return {};
  unsigned distance=(heads_[column]+rows_-row)%rows_;
  if(distance>=lengths_[column])return {};
  static const char glyphs[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:<>";
  uint32_t hash=(column+1)*2654435761u^(row+1)*2246822519u^(frame_/3)*3266489917u^random_;
  return {glyphs[hash%(sizeof(glyphs)-1)],uint8_t(distance==0?3:distance<3?2:1)};
}
}
