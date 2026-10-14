#include "phone_core.h"
#include "recon_core.h"
#include "capture_format.h"
#include "vendor_db.h"
#include "lab_tools.h"
#include <assert.h>
#include "attack_frames.h"
#include "ir_format.h"
#include <string>
#include <vector>
#include <stdio.h>
#include <cstring>
using namespace hackerphone;
struct Wire:AtTransport { std::string bytes; bool good=true; bool write(const char* p,size_t n)override{if(!good)return false;bytes.append(p,n);return true;} };
static void rx(PhoneCore& p,const char* text,uint32_t now=100){p.feed(reinterpret_cast<const uint8_t*>(text),strlen(text),now);}
static void init(PhoneCore& p,uint32_t now=0){p.begin(now);for(unsigned i=0;i<5;++i){rx(p,"\r\nOK\r\n",now+i);if(i<4)p.tick(now+i+1);}}
static void ready(PhoneCore& p){init(p);rx(p,"+CPIN: READY\r\n+CEREG: 2,1\r\n");}
int main(){
  unsigned groups=0;
  {
    Wire w;PhoneCore p(w);init(p);p.tick(10);rx(p,"+CME ERROR: SIM not inserted\r\n",11);
    assert(p.state().sim==Sim::Absent&&p.state().online);
    assert(p.dial("+15551234567",12)==Request::NoSim);
    assert(p.sendSms("+15551234567","hello",12)==Request::NoSim);
    for(uint32_t t=20;t<30000;t+=20)p.tick(t);
    assert(w.bytes.find("QPOWD")==std::string::npos&&w.bytes.find("ATD")==std::string::npos);
    assert(p.state().timeouts==0);++groups;
  }
  {
    Wire w;PhoneCore p(w);ready(p);
    assert(p.dial("123;AT+QPOWD",101)==Request::Invalid);
    assert(p.dial("+15551234567",102)==Request::Accepted);
    assert(p.dial("123",103)==Request::Busy);
    rx(p,"OK\r\n",104);assert(p.state().call==Call::Dialing);
    p.tick(105);rx(p,"+CLCC: 1,0,0,0,0\r\nOK\r\n",106);
    assert(p.state().call==Call::Active&&p.state().phonePriority());
    assert(p.hangup(107)==Request::Accepted);rx(p,"OK\r\n",108);assert(p.state().call==Call::Idle);++groups;
  }
  {
    Wire w;PhoneCore p(w);ready(p);rx(p,"RING\r\n+CLIP: \"+15550001111\",145\r\n");
    assert(p.state().call==Call::Ringing&&!strcmp(p.state().caller,"+15550001111"));
    assert(p.answer(101)==Request::Accepted);rx(p,"OK\r\n",102);assert(p.state().call!=Call::Active);
    rx(p,"NO CARRIER\r\n",103);assert(!p.state().phonePriority());++groups;
  }
  {
    SmsPdu pdu;assert(encodeSms("+12345","A",pdu)==Request::Accepted);
    assert(!strcmp(pdu.hex,"00010005912143F50008020041"));assert(pdu.tpduBytes==12);
    assert(encodeSms("+12345",u8"é\n中",pdu)==Request::Accepted&&pdu.characters==3);
    assert(encodeSms("123",std::string(70,'a').c_str(),pdu)==Request::Accepted);
    assert(encodeSms("123",std::string(71,'a').c_str(),pdu)==Request::TooLong);
    assert(encodeSms("123",u8"😀",pdu)==Request::UnsupportedText);
    assert(encodeSms("123","\xc0\xaf",pdu)==Request::UnsupportedText);
    assert(encodeSms("123","\xed\xa0\x80",pdu)==Request::UnsupportedText);
    assert(encodeSms("123","\xe2",pdu)==Request::UnsupportedText);++groups;
  }
  {
    Wire w;PhoneCore p(w);ready(p);w.bytes.clear();
    assert(p.sendSms("+12345","A\"\x1a",101)==Request::Accepted);
    rx(p,"OK\r\n",102);assert(w.bytes.find("AT+CMGS=")!=std::string::npos);
    rx(p,"\r\n> ",103);assert(w.bytes.back()==26);
    assert(p.state().smsOutcome==Outcome::Pending);
    rx(p,"+CMGS: 2\r\n",104);assert(p.state().smsOutcome==Outcome::Pending);
    rx(p,"OK\r\n",105);assert(p.state().smsOutcome==Outcome::Accepted);++groups;
  }
  {
    Wire w;PhoneCore p(w);ready(p);p.sendSms("123","hello",101);rx(p,"OK\r\n",102);rx(p,"> ",103);
    p.tick(120104);assert(p.state().smsOutcome==Outcome::Uncertain&&!p.state().online);
    std::string writes=w.bytes;rx(p,"+CMGS: 7\r\nOK\r\n",120105);p.tick(900000);
    assert(p.state().smsOutcome==Outcome::Uncertain&&w.bytes==writes);++groups;
  }
  {
    Wire w;PhoneCore p(w);ready(p);p.sendSms("123","hello",101);rx(p,"OK\r\n",102);
    rx(p,"+CPIN: NOT INSERTED\r\n",103);assert(p.state().smsOutcome==Outcome::Failed);
    auto sent=w.bytes.size();rx(p,"> ",104);assert(w.bytes.size()==sent);++groups;
  }
  {
    Wire w;PhoneCore p(w);ready(p);rx(p,"+CSQ: 99,99\r\n");assert(!p.state().signalKnown);
    rx(p,"+CSQ: 20,0\r\n");assert(p.state().signalDbm==-73);
    rx(p,"+CEREG: 5,\"1234\",\"56789\",7\r\n");assert(p.state().registration==5);
    rx(p,"+CEREG: 2,3\r\n");assert(!p.state().registered());
    rx(p,"+CMTI: \"SM\",17\r\n");assert(p.state().newestSmsIndex==17&&p.state().inboxNotifications==1);++groups;
  }
  {
    Wire w;PhoneCore p(w);init(p,0xfffffff0u);p.tick(3);rx(p,"+CPIN: READY\r\nOK\r\n",4);
    assert(p.state().timeouts==0);p.feed(reinterpret_cast<const uint8_t*>(std::string(800,'a').data()),800,5);rx(p,"\r\nRING\r\n",6);
    assert(p.state().malformedLines==1&&p.state().call==Call::Ringing);++groups;
  }
  {
    Wire w;PhoneCore p(w);ready(p);w.good=false;assert(p.dial("123",101)==Request::Accepted);
    assert(p.state().callOutcome==Outcome::Failed&&p.state().call==Call::Idle);++groups;
  }
  {
    assert(safeRelativePath("sessions/session-01.csv"));
    for(auto p:{"","/root","../x","x/../y","x//y","x/","C:/x","x\\y","%2e%2e/x"})assert(!safeRelativePath(p));
    char text[80];assert(csvField(text,sizeof(text),"=HYPERLINK(\"x\")"));assert(!strcmp(text,"\"'=HYPERLINK(\"\"x\"\")\""));
    assert(csvField(text,sizeof(text),"  =SUM(1)")&&text[1]=='\'');
    assert(!csvField(text,4,"long")&&text[0]==0);
    uint8_t h[24],record[16];pcapHeader(h);assert(h[0]==0xd4&&h[20]==105);
    assert(pcapRecord(record,123456789,500,800));assert(record[0]==123&&record[8]==0xf4&&record[9]==1);
    assert(!pcapRecord(record,0,1025,1025));assert(!pcapRecord(record,0,900,800));++groups;
  }
  {
    ReconCore r;const uint8_t ad[]={8,9,'F','l','i','p','p','e','r'};uint8_t mac[]={0,1,2,3,4,5};
    int i=r.ble(ad,sizeof(ad),mac,0,-65,100);assert(i>=0&&r.targets[i].match.family==dragon::Family::Flipper);
    assert(r.targets[i].match.confidence==dragon::Confidence::Low);
    assert(r.ble(ad,sizeof(ad),mac,1,-65,100)!=i); // BLE address type is part of identity.
    r.selected=i;
    for(unsigned n=6;n<100;++n){mac[5]=uint8_t(n);r.ble(ad,sizeof(ad),mac,0,-65,n*100);}
    assert(r.targets[i].mac[5]==5&&r.evictions>0);
    r.selected=-1;r.pinned=i;
    for(unsigned n=101;n<200;++n){mac[5]=uint8_t(n);r.ble(ad,sizeof(ad),mac,0,-65,n*100);}
    assert(r.targets[i].mac[5]==5); // The open detail page also protects identity.
    assert(r.count(50000)==0);++groups;
  }
  {
    ReconCore r;uint32_t rng=17;uint8_t data[255],mac[6]={0,1,2,3,4,5};
    for(unsigned i=0;i<20000;++i){rng=rng*1664525+1013904223;size_t n=rng%256;
      for(size_t j=0;j<n;++j){rng=rng*1664525+1013904223;data[j]=uint8_t(rng>>24);}
      r.ble(data,n,mac,0,-50,i);r.wifi(data,n,-50,1,i);
    }++groups;
  }
  {
    struct Memory:VendorReader{std::vector<uint8_t> bytes;size_t size()const override{return bytes.size();}bool read(size_t offset,void* p,size_t n)override{if(offset>bytes.size()||n>bytes.size()-offset)return false;memcpy(p,bytes.data()+offset,n);return true;}} m;
    m.bytes.resize(16+52*2);memcpy(m.bytes.data(),"HPOUI1\0\0",8);m.bytes[8]=2;m.bytes[12]=52;
    m.bytes[16]=0x33;m.bytes[17]=0x22;m.bytes[18]=0x10;memcpy(m.bytes.data()+20,"Example Corp",13);
    m.bytes[68]=0x66;m.bytes[69]=0x55;m.bytes[70]=0x44;memcpy(m.bytes.data()+72,"Another Corp",13);
    VendorDb db;assert(db.open(m)&&db.count()==2);char name[48];uint8_t mac[]={0x10,0x22,0x33,4,5,6};
    assert(db.lookup(mac,true,0,name)&&!strcmp(name,"Example Corp"));assert(db.lookup(mac,false,0,name));
    assert(!db.lookup(mac,true,1,name)&&!*name);mac[0]=0x44;mac[1]=0x55;mac[2]=0x66;assert(db.lookup(mac,true,0,name)&&!strcmp(name,"Another Corp"));
    mac[0]=0x46;assert(!db.lookup(mac,false,0,name));mac[0]=0x40;assert(!db.lookup(mac,true,0,name));
    m.bytes[12]=51;assert(!db.open(m)&&db.count()==0);m.bytes[12]=52;m.bytes.pop_back();assert(!db.open(m));
    m.bytes.push_back(0);m.bytes[8]=255;assert(!db.open(m));m.bytes[8]=2;assert(db.open(m));
    memset(m.bytes.data()+20,'X',48);mac[0]=0x10;mac[1]=0x22;mac[2]=0x33;assert(!db.lookup(mac,true,0,name));
    assert(!strcmp(addressKind(mac,true,1),"Private nonresolvable"));mac[0]=0x40;assert(!strcmp(addressKind(mac,true,1),"Private rotating BLE"));++groups;
  }
  {
    uint8_t frame[128],mac[]={0x10,0x22,0x33,4,5,6};size_t n=makeBeacon(frame,sizeof(frame),mac,6,"HackerPhone-Lab");
    assert(n==66);auto f=dragon::parseWifi(frame,n);assert(f.valid&&f.beacon&&f.channel==6&&!strcmp(f.name,"HackerPhone-Lab")&&!memcmp(f.source,mac,6));
    assert(!makeBeacon(frame,20,mac,1,"test"));assert(!makeBeacon(frame,sizeof(frame),mac,12,"test"));assert(!makeBeacon(frame,sizeof(frame),mac,1,""));
    uint8_t ad[]={2,1,6,4,9,'L','a','b'};assert(validLegacyAdvert(ad,sizeof(ad)));ad[3]=9;assert(!validLegacyAdvert(ad,sizeof(ad)));assert(!validLegacyAdvert(frame,32));++groups;
  }
  {
    ReconCore r;uint8_t frame[26]{};frame[0]=0xc0;frame[10]=0x10;frame[24]=7;
    for(unsigned i=0;i<6;++i){frame[22]=i<<4;r.wifi(frame,sizeof(frame),-50,6,100+i*100);}
    assert(r.traffic.disconnectFrames==6&&r.traffic.disconnectAlerts==1&&r.eventCount==1&&r.traffic.reason==7);
    frame[1]=8;r.wifi(frame,sizeof(frame),-50,6,701);assert(r.traffic.disconnectFrames==6);
    assert(!r.traffic.disconnectActive(31000));assert(r.channelFrames[6]==7);
    uint8_t mac[]={0x10,1,2,3,4,5},beacon[128];auto n=makeBeacon(beacon,sizeof(beacon),mac,3,"Lab");int i=r.wifi(beacon,n,-63,3,800);assert(i>=0&&r.targets[i].accessPoint&&r.targets[i].peak==-63);
    beacon[0]=0x08;beacon[1]=1;memcpy(beacon+4,mac,6);beacon[10]=0x12;int sta=r.wifi(beacon,24,-64,3,810);assert(sta>=0&&r.targets[sta].station&&!memcmp(r.targets[sta].bssid,mac,6));
    dragon::BleBurstMonitor burst;dragon::DeviceKey key;key.radio=1;key.addressType=1;dragon::BleBurstResult result;
    for(unsigned k=0;k<12;++k){key.mac[5]=k;result=burst.observe(key,1,k,1000+k);}
    assert(result.alert&&result.addresses==12);result=burst.observe(key,1,11,1013);assert(!result.alert);++groups;
  }
  {
    AttackTarget t;const uint8_t ap[]={0x10,0x22,0x33,4,5,6},sta[]={0x12,1,2,3,4,5};
    memcpy(t.bssid,ap,6);memcpy(t.client,sta,6);t.hasAp=t.hasClient=true;t.channel=1;t.destinationChannel=6;
    uint8_t entropy[96]{},bytes[258];
    for(unsigned round=0;round<256;++round){for(unsigned j=0;j<96;++j)entropy[j]=round+j;
      for(unsigned m=0;m<unsigned(AttackMode::Count);++m){auto mode=AttackMode(m);memset(bytes,0xa5,sizeof(bytes));
        size_t n=attackDefinitions[m].ble?buildBleAttack(bytes+1,256,mode,entropy):buildWifiAttack(bytes+1,256,mode,t,sta,round,entropy);
        assert(n&&n<=256&&bytes[0]==0xa5&&bytes[n+1]==0xa5);
        if(attackDefinitions[m].ble)assert(validLegacyAdvert(bytes+1,n));
        else{assert(n>=24);assert(bytes[23]==uint8_t(round<<4));}
        memset(bytes,0xa5,sizeof(bytes));
        size_t shortResult=attackDefinitions[m].ble?buildBleAttack(bytes+1,n-1,mode,entropy):buildWifiAttack(bytes+1,n-1,mode,t,sta,round,entropy);
        assert(!shortResult);for(auto c:bytes)assert(c==0xa5);
      }
    }
    assert(!validAttackTarget(AttackMode::Count,t));t.hasAp=false;assert(!validAttackTarget(AttackMode::Deauthentication,t));
    t.hasAp=true;t.hasClient=false;assert(!validAttackTarget(AttackMode::BadMessage,t));t.hasClient=true;
    t.client[0]=0xff;assert(!validAttackTarget(AttackMode::BadMessage,t));t.client[0]=0x12;
    t.destinationChannel=t.channel;assert(!validAttackTarget(AttackMode::ChannelSwitch,t));
    t.channel=12;assert(!validAttackTarget(AttackMode::Beacon,t));t.channel=1;memset(t.ssid,'x',sizeof(t.ssid));assert(!validAttackTarget(AttackMode::Beacon,t));
    ++groups;
  }
  {
    struct Input:IrInput{std::string bytes;size_t pos=0;explicit Input(std::string s):bytes(s){}int read()override{return pos<bytes.size()?uint8_t(bytes[pos++]):-1;}};
    const std::string header="Filetype: IR signals file\nVersion: 1\n# Example remote\n";
    const std::string raw="name: Power\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 9000 4500 560 560 560 1690 560\n";
    IrReader reader;IrSignal signal;char error[80];
    Input good(header+raw+"# Next button\nname: Volume\ntype: parsed\nprotocol: NEC\naddress: 10 00 00 00\ncommand: 34 00 00 00\n");
    assert(reader.next(good,signal,error,sizeof(error))==IrReadResult::Ready&&signal.count==7&&signal.durations[5]==1690&&!strcmp(signal.name,"Power"));
    assert(reader.next(good,signal,error,sizeof(error))==IrReadResult::Ready&&signal.count==67&&!strcmp(signal.name,"Volume"));
    // Decode timings independently to verify the NEC address/command inverse
    // bytes and their transmitted bit order, rather than testing only sizes.
    uint32_t bits=0;for(unsigned i=0;i<32;++i){assert(signal.durations[2+i*2]==560);if(signal.durations[3+i*2]>1000)bits|=1u<<i;}
    assert(bits==0xcb34ef10);assert(validIrSignal(signal));
    assert(reader.next(good,signal,error,sizeof(error))==IrReadResult::End);
    for(const auto& bad:{raw,header+"name: X\ntype: raw\nfrequency: 38000\nduty_cycle: nan\ndata: 1 2\n",header+"name: X\ntype: raw\nfrequency: 38000\nduty_cycle: .33\ndata: 0 500\n",header+"name: X\ntype: raw\nfrequency: 38000\nduty_cycle: .33\ndata: 1000001 3\n",header+"name: X\nname: Y\n"}){
      reader.reset();Input input(bad);assert(reader.next(input,signal,error,sizeof(error))==IrReadResult::Invalid&&signal.count==0);
    }
    std::string large=header+"name: Max\ntype: raw\nfrequency: 38000\nduty_cycle: .33\ndata:";for(unsigned i=0;i<1024;++i)large+=" 500";
    reader.reset();Input max(large+"\n");assert(reader.next(max,signal,error,sizeof(error))==IrReadResult::Ready&&signal.count==1024);
    reader.reset();Input over(large+" 500\n");assert(reader.next(over,signal,error,sizeof(error))==IrReadResult::Invalid&&signal.count==0);
    reader.reset();Input budget(header+"name: X\ntype: raw\nfrequency: 38000\nduty_cycle: .33\ndata: 1 2"+std::string(65536,' '));assert(reader.next(budget,signal,error,sizeof(error))==IrReadResult::Invalid);
    reader.reset();Input unsupported(header+"name: Other\ntype: parsed\nprotocol: Unknown\naddress: 00 00 00 00\ncommand: 00 00 00 00\n"+raw);
    assert(reader.next(unsupported,signal,error,sizeof(error))==IrReadResult::Unsupported&&signal.count==0);
    assert(reader.next(unsupported,signal,error,sizeof(error))==IrReadResult::Ready);
    uint32_t value;assert(parseIrHex32("12 34 56 78",value)&&value==0x78563412);for(auto bad:{"1","12 34","12 34 56 78 90","-1 00 00 00","1234 56 78 90"})assert(!parseIrHex32(bad,value));
    assert(validIrLibraryPath("/hackerphone/ir/database/Sony TV/Remote 1.ir"));
    for(auto path:{"/other.ir","/hackerphone/ir/../secret","/hackerphone/ir/","/hackerphone/ir/x//a.ir","/hackerphone/ir/./x.ir","/hackerphone/ir/x\\a.ir","/hackerphone/ir/x\na.ir"})assert(!validIrLibraryPath(path));
    for(auto protocol:{"NEC","NECext","Samsung32","SIRC","SIRC15"})assert(irEncodeParsed(signal,protocol,1,2)&&validIrSignal(signal));
    assert(!irEncodeParsed(signal,"NEC",256,1));assert(!irEncodeParsed(signal,"RC6",1,2));++groups;
  }
  printf("PASS %u test groups: phone, SMS, recon, storage, vendor DB, traffic, attack frame bounds and IR files; 40,000 malformed/random radio frames\n",groups);
}
