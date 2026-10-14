// Original MIT implementation of the documented .ir interchange format.
// No Flipper application code or IR database is incorporated.
#include "ir_format.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

namespace hackerphone {
bool validIrLibraryPath(const char* path){
  constexpr char prefix[]="/hackerphone/ir/";
  if(!path||strncmp(path,prefix,sizeof(prefix)-1)||strlen(path)>=192)return false;
  const char* part=path+sizeof(prefix)-1;
  for(const char* p=part;;++p){unsigned char c=*p;if(c&&c!='/'&&(c<32||c==127||c=='\\'||c==':'))return false;
    if(!c||c=='/'){size_t n=p-part;if(!n||(n==1&&part[0]=='.')||(n==2&&part[0]=='.'&&part[1]=='.'))return false;if(!c)break;part=p+1;}}
  return true;
}
bool validIrSignal(const IrSignal& s){
  if(s.count<2||s.count>IrSignal::Capacity||s.frequency<10000||s.frequency>100000||!isfinite(s.duty)||s.duty<.1f||s.duty>.5f)return false;
  uint64_t total=0;for(unsigned i=0;i<s.count;++i){if(!s.durations[i]||s.durations[i]>1000000)return false;total+=s.durations[i];}
  return total<=2000000;
}
static int hexDigit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
bool parseIrHex32(const char* text,uint32_t& value){
  if(!text)return false;value=0;
  for(unsigned i=0;i<4;++i){while(*text==' '||*text=='\t')++text;int hi=hexDigit(*text);if(hi<0)return false;++text;int lo=hexDigit(*text);if(lo<0)return false;++text;value|=uint32_t((hi<<4)|lo)<<(i*8);if(i<3&&*text!=' '&&*text!='\t')return false;}
  while(*text==' '||*text=='\t')++text;return !*text;
}
static void push(IrSignal& s,uint32_t n){if(s.count<IrSignal::Capacity)s.durations[s.count++]=n;}
static void bits(IrSignal& s,uint32_t value,unsigned count,uint32_t mark,uint32_t zero,uint32_t one){for(unsigned i=0;i<count;++i){push(s,mark);push(s,(value&(1u<<i))?one:zero);}}
bool irEncodeParsed(IrSignal& s,const char* protocol,uint32_t address,uint32_t command){
  if(!protocol)return false;s.count=0;s.duty=.33f;s.frequency=38000;
  if(!strcmp(protocol,"NEC")||!strcmp(protocol,"NECext")||!strcmp(protocol,"Samsung32")){
    bool extended=!strcmp(protocol,"NECext"),samsung=!strcmp(protocol,"Samsung32");
    if(address>(extended?65535u:255u)||command>(extended?65535u:255u))return false;
    uint16_t a=extended?address:uint16_t(address|((samsung?address:(~address&255))<<8));
    uint16_t c=extended?command:uint16_t(command|((~command&255)<<8));
    push(s,samsung?4500:9000);push(s,4500);bits(s,uint32_t(a)|(uint32_t(c)<<16),32,560,560,1690);push(s,560);return true;
  }
  unsigned width=!strcmp(protocol,"SIRC")?12:!strcmp(protocol,"SIRC15")?15:0;
  if(width){
    if(command>127||address>=(1u<<(width-7)))return false;s.frequency=40000;
    push(s,2400);push(s,600);uint32_t value=command|(address<<7);
    for(unsigned i=0;i<width;++i){push(s,value&(1u<<i)?1200:600);push(s,600);}return true;
  }
  return false;
}
static char* trim(char* s){while(*s==' '||*s=='\t')++s;size_t n=strlen(s);while(n&&(s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'))s[--n]=0;return s;}
IrReadResult IrReader::next(IrInput& in,IrSignal& out,char* error,size_t capacity){
  out=IrSignal{};char type[16]{},protocol[24]{};uint32_t address=0,command=0;unsigned fields=0;
  // Stop a malformed unbroken record from monopolizing the application.
  unsigned budget=65536;
  auto fail=[&](const char* why,IrReadResult r=IrReadResult::Invalid){out.count=0;if(error&&capacity)snprintf(error,capacity,"%s",why);return r;};
  auto get=[&](){if(!budget)return -2;--budget;return in.read();};
  while(budget){
    char key[32]{};unsigned used=0;int ch;bool longKey=false;
    do{ch=get();}while(ch=='\r'||ch=='\n'||ch==' '||ch=='\t');
    if(ch==-1)return fields?fail("Incomplete IR record"):IrReadResult::End;
    if(ch==-2)return fail("IR record exceeds read limit");
    if(ch=='#'){while((ch=get())>=0&&ch!='\n'){};continue;}
    while(ch>=0&&ch!=':'&&ch!='\n'){
      if(used+1<sizeof(key))key[used++]=char(ch);else longKey=true;ch=get();
    }
    if(longKey||ch!=':')return fail("Invalid IR field");
    char* label=trim(key);
    if(!strcmp(label,"data")){
      if(fields&64)return fail("Duplicate raw data");uint32_t number=0;bool digits=false;out.count=0;
      while((ch=get())>=0&&ch!='\n'){
        if(ch>='0'&&ch<='9'){if(number>100000)return fail("IR duration too large");number=number*10+ch-'0';digits=true;}
        else if(ch==' '||ch=='\t'||ch=='\r'){
          if(digits){if(!number||out.count>=IrSignal::Capacity)return fail("IR duration/count limit");out.durations[out.count++]=number;number=0;digits=false;}
        }else return fail("Invalid raw duration");
      }
      if(digits){if(!number||out.count>=IrSignal::Capacity)return fail("IR duration/count limit");out.durations[out.count++]=number;}
      if(ch==-2)return fail("IR record exceeds read limit");
      fields|=64;
    }else{
      char value[160]{};used=0;bool overflow=false;
      while((ch=get())>=0&&ch!='\n'){if(used+1<sizeof(value))value[used++]=char(ch);else overflow=true;}
      if(ch==-2)return fail("IR record exceeds read limit");
      if(overflow)return fail("IR field too long");char* v=trim(value);
      if(!strcmp(label,"Filetype")){header_=!strcmp(v,"IR signals file")||!strcmp(v,"IR library file");if(!header_)return fail("Unsupported IR file type");}
      else if(!strcmp(label,"Version")){version_=!strcmp(v,"1");if(!version_)return fail("Unsupported IR version");}
      else if(!strcmp(label,"name")){if(fields)return fail("Incomplete prior IR record");if(!*v||strlen(v)>=sizeof(out.name))return fail("Invalid signal name");for(const char* p=v;*p;++p)if(*p<32||*p>126)return fail("Invalid signal name");strcpy(out.name,v);fields|=1;}
      else if(!strcmp(label,"type")){if(fields&2||strlen(v)>=sizeof(type))return fail("Invalid signal type");strcpy(type,v);fields|=2;}
      else if(!strcmp(label,"protocol")){if(fields&4||strlen(v)>=sizeof(protocol))return fail("Invalid protocol");strcpy(protocol,v);fields|=4;}
      else if(!strcmp(label,"address")){if(fields&8||!parseIrHex32(v,address))return fail("Invalid address");fields|=8;}
      else if(!strcmp(label,"command")){if(fields&16||!parseIrHex32(v,command))return fail("Invalid command");fields|=16;}
      else if(!strcmp(label,"frequency")){char* end;unsigned long n=strtoul(v,&end,10);if(fields&32||end==v||*end||n<10000||n>100000)return fail("Invalid frequency");out.frequency=n;fields|=32;}
      else if(!strcmp(label,"duty_cycle")){char* end;float duty=strtof(v,&end);if(fields&128||end==v||*end||!isfinite(duty)||duty<.1f||duty>.5f)return fail("Invalid duty cycle");out.duty=duty;fields|=128;}
    }
    if((fields&3)==3&&!strcmp(type,"parsed")&&(fields&31)==31){
      if(!header_||!version_)return fail("IR header missing");
      if(!irEncodeParsed(out,protocol,address,command))return fail("Unsupported protocol/value",IrReadResult::Unsupported);
      if(error&&capacity)*error=0;return IrReadResult::Ready;
    }
    if((fields&227)==227&&!strcmp(type,"raw")){
      if(!header_||!version_)return fail("IR header missing");if(!validIrSignal(out))return fail("Invalid raw signal");
      if(error&&capacity)*error=0;return IrReadResult::Ready;
    }
  }
  return fail("IR record exceeds read limit");
}
}
