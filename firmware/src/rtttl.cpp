#include "rtttl.h"
#include <string.h>
#include <math.h>
#include <ctype.h>
namespace hackerphone {
bool parseRtttl(const char* p,RtttlTone& out){
 out={};if(!p||strlen(p)>RtttlMaxText)return false;const char* colon=strchr(p,':');if(!colon||colon==p||colon-p>24)return false;
 memcpy(out.name,p,colon-p);p=colon+1;unsigned duration=4,octave=5,bpm=120,seen=0;
 auto spaces=[&](){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;};
 auto number=[&](unsigned& n){n=0;const char* start=p;while(isdigit(uint8_t(*p))){n=n*10+(*p++-'0');if(n>10000)return false;}return p!=start;};
 while(*p&&*p!=':'){spaces();if(!*p)return false;char key=tolower(uint8_t(*p++));if(*p++!='=')return false;unsigned n;if(!number(n))return false;
   if(key=='d'&&!(seen&1)){if(!n||n>64||(n&(n-1)))return false;duration=n;seen|=1;}
   else if(key=='o'&&!(seen&2)){if(n<3||n>8)return false;octave=n;seen|=2;}
   else if(key=='b'&&!(seen&4)){if(n<25||n>900)return false;bpm=n;seen|=4;}else return false;
   spaces();if(*p==','){++p;spaces();if(*p==':')return false;}else if(*p!=':')return false;
 }
 if(*p++!=':')return false;spaces();
 while(*p){if(out.count==RtttlMaxNotes)return false;unsigned d=duration,o=octave;bool dotted=false;spaces();if(isdigit(uint8_t(*p))&&(!number(d)||!d||d>64||(d&(d-1))))return false;
   char note=tolower(uint8_t(*p++));int semitone=note=='c'?0:note=='d'?2:note=='e'?4:note=='f'?5:note=='g'?7:note=='a'?9:note=='b'?11:note=='p'?-1:-2;if(semitone==-2)return false;
   if(*p=='#'){if(semitone<0)return false;++semitone;++p;}
   if(*p=='.'){dotted=true;++p;}if(isdigit(uint8_t(*p))){o=*p++-'0';if(o<3||o>8)return false;}
   if(*p=='.'){if(dotted)return false;dotted=true;++p;}
   unsigned ms=240000/bpm/d;if(dotted)ms=ms*3/2;if(!ms||out.duration+ms>RtttlMaxDuration)return false;
   unsigned hz=semitone<0?0:unsigned(lroundf(440.f*powf(2.f,(int(o)*12+semitone-57)/12.f)));if(hz>8000)return false;
   out.notes[out.count++]={uint16_t(hz),uint16_t(ms)};out.duration+=ms;spaces();if(!*p)break;if(*p++!=',')return false;spaces();if(!*p)return false;
 }return out.count>0;
}
}
