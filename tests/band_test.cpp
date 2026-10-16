#include "band_core.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
using namespace hackerband;
int main(){
  Session phone,band;Packet p{},q{};uint8_t bytes[MaxFrame+2]{};
  assert(!phone.open(1,false,true));assert(!phone.open(1,true,false));assert(!phone.open(0,true,true));
  assert(phone.open(42,true,true)&&band.open(42,true,true));
  uint8_t payload[]={1,2,3};assert(phone.make(Kind::Message,payload,sizeof(payload),p));
  auto n=encode(p,bytes,MaxFrame);assert(n==17&&decode(bytes,n,q)&&band.accept(q));
  assert(!band.accept(q));q.session=41;q.sequence=2;assert(!band.accept(q));
  bytes[n]=99;assert(!decode(bytes,n+1,q));
  for(size_t i=0;i<n;++i)assert(!decode(bytes,i,q));
  bytes[2]=2;assert(!decode(bytes,n,q));bytes[2]=1;
  bytes[12]=255;assert(!decode(bytes,n,q));
  assert(!phone.make(Kind::Ring,nullptr,1,p));assert(!phone.make(Kind(255),nullptr,0,p));
  assert(!phone.make(Kind::Message,bytes,MaxPayload+1,p));
  phone.close();assert(!phone.make(Kind::Ring,nullptr,0,p));
  assert(band.open(43,true,true));q.session=42;q.sequence=100;assert(!band.accept(q));

  Stopwatch watch;watch.toggle(0xfffffff0u);assert(watch.elapsed(0x20)==48);
  watch.tick(0x20);watch.toggle(100);assert(watch.elapsed(500)==116);
  watch.toggle(1000);watch.tick(2000);assert(watch.elapsed(2500)==1616);
  watch.reset(2500);assert(!watch.running()&&watch.elapsed(4000)==0);
  // Tick at least once per millis wrap; accumulation may exceed 32 bits.
  watch.toggle(0);watch.tick(0xfffffff0u);watch.tick(100);assert(watch.elapsed(100)==4294967396ull);

  Clock clock;assert(!clock.valid()&&clock.local(123)==0);
  assert(!clock.set(123,0,0));assert(!clock.set(1770000000,841,0));
  assert(clock.set(1770000000,-240,0xfffffff0u));clock.tick(0x3d8);
  assert(clock.utc(0x3d8)==1770000001&&clock.local(0x3d8)==1769985601);
  assert(!clock.set(1,0,4)&&clock.valid());

  IncomingCall call;assert(call.ignore()==CallAction::None);
  assert(call.ring(1,"+15550000000",100)&&call.audible());
  assert(call.ignore()==CallAction::Ignore&&!call.audible()&&call.active());
  assert(call.ring(1,"+15550000000",1000)&&!call.audible());
  assert(call.decline()==CallAction::Decline&&call.declinePending());
  assert(call.decline()==CallAction::None);call.declineResult(false);assert(!call.declinePending());
  call.end(2);assert(call.active());call.end(1);assert(!call.active());
  call.ring(2,"new caller",2000);assert(call.audible());call.tick(17000);assert(!call.active());
  call.ring(3,"caller",0xfffffff0u);call.tick(20);assert(call.active());call.disconnect();assert(!call.audible());
  assert(canDecline(7,7,true,true));assert(!canDecline(7,8,true,true));
  assert(!canDecline(7,7,false,true));assert(!canDecline(7,7,true,false));assert(!canDecline(0,0,true,true));

  Inbox inbox;assert(!inbox.latest());for(uint32_t i=1;i<=9;++i)assert(inbox.push(i,"Text","hello\nworld"));
  assert(inbox.size()==4&&inbox.latest()->id==9&&!strcmp(inbox.latest()->body,"hello world"));
  assert(!inbox.push(9,"duplicate","body"));inbox.clear();assert(!inbox.latest()&&inbox.size()==0);

  AlertGate alerts;assert(!alerts.observe(1,1,0));assert(!alerts.observe(1,2,0));
  assert(!alerts.observe(1,2,100));assert(!alerts.observe(1,2,1000));assert(alerts.observe(1,2,2000));
  assert(!alerts.observe(2,3,3000));assert(!alerts.observe(2,3,4000));assert(!alerts.observe(2,3,5000));
  assert(!alerts.observe(2,3,62000));assert(!alerts.observe(2,3,63000));assert(alerts.observe(2,3,64000));
  alerts.reset();assert(!alerts.observe(0,3,0));assert(!alerts.observe(1,4,0));

  MatrixRain rain;assert(!rain.begin(41,20,1,0));assert(!rain.begin(20,3,1,0));
  assert(rain.begin(20,16,42,0xfffffff0u));rain.tick(104);rain.interact(105);rain.tick(165);
  unsigned visible=0;for(uint8_t x=0;x<20;++x)for(uint8_t y=0;y<16;++y){
    auto c=rain.cell(x,y);assert(c.brightness<=3);if(c.brightness){++visible;assert(c.glyph!=' ');}
  }
  assert(visible>0&&!rain.cell(20,0).brightness&&!rain.cell(0,16).brightness);
  rain.tick(600000);assert(rain.cell(0,0).brightness<=3);

  // Deterministic fuzz: bounded decode must reject or produce an encodable frame.
  uint32_t random=0x12345678;for(unsigned i=0;i<40000;++i){
    for(auto& b:bytes){random^=random<<13;random^=random>>17;random^=random<<5;b=uint8_t(random);}
    if(decode(bytes,random%sizeof(bytes),q)){uint8_t out[MaxFrame];assert(encode(q,out,sizeof(out)));}
  }
  puts("PASS: band protocol, authenticated-session guard, clock, stopwatch, call actions, inbox, alert cooldown, Matrix animation and 40000 malformed packets");
}
