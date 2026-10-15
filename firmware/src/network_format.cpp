#include "network_format.h"
#include <stdio.h>
#include <string.h>
#include <string_view>
#include <ctype.h>
namespace hackerphone {
HttpParse parsePortalRequest(const char* bytes,size_t length,PortalRequest& output){
  if(!bytes)return HttpParse::Invalid;std::string_view data(bytes,length);auto end=data.find("\r\n\r\n");
  if(end==data.npos)return length>=1024?HttpParse::TooLarge:HttpParse::Incomplete;if(end+4>1024)return HttpParse::TooLarge;
  auto header=data.substr(0,end);for(unsigned char c:header)if((c<32&&c!='\r'&&c!='\n'&&c!='\t')||c>=127)return HttpParse::Invalid;
  size_t first=header.find("\r\n");auto request=header.substr(0,first);bool post=request.substr(0,5)=="POST ";
  if(!post&&request.substr(0,4)!="GET ")return HttpParse::Invalid;
  auto space=request.rfind(' ');if(space==request.npos||(request.substr(space+1)!="HTTP/1.1"&&request.substr(space+1)!="HTTP/1.0"))return HttpParse::Invalid;
  auto equal=[](std::string_view a,std::string_view b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(tolower(uint8_t(a[i]))!=b[i])return false;return true;};
  size_t body=0;bool hasLength=false;size_t at=first==header.npos?header.size():first+2;
  while(at<header.size()){
    size_t next=header.find("\r\n",at);if(next==header.npos)next=header.size();auto line=header.substr(at,next-at);size_t colon=line.find(':');if(colon==line.npos||!colon)return HttpParse::Invalid;
    auto name=line.substr(0,colon),value=line.substr(colon+1);while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.remove_prefix(1);while(!value.empty()&&(value.back()==' '||value.back()=='\t'))value.remove_suffix(1);
    if(equal(name,"transfer-encoding"))return HttpParse::Invalid;
    if(equal(name,"content-length")){if(hasLength||value.empty())return HttpParse::Invalid;hasLength=true;for(char c:value){if(c<'0'||c>'9')return HttpParse::Invalid;body=body*10+c-'0';if(body>512)return HttpParse::TooLarge;}}
    at=next+2;
  }
  if(post&&!hasLength)return HttpParse::Invalid;if(!post&&body)return HttpParse::Invalid;
  if(length<end+4+body)return HttpParse::Incomplete;output={post,end+4,body};return HttpParse::Ready;
}
bool parseIpv4(const char* s,uint32_t& value){
  if(!s)return false;uint32_t out=0;
  for(unsigned i=0;i<4;++i){unsigned part=0,digits=0;while(*s>='0'&&*s<='9'){part=part*10+(*s++-'0');if(++digits>3||part>255)return false;}
    if(!digits||(i<3?*s++!='.':*s!=0))return false;out=(out<<8)|part;}
  value=out;return true;
}
void formatIpv4(uint32_t v,char out[16]){snprintf(out,16,"%u.%u.%u.%u",unsigned(v>>24),unsigned(v>>16&255),unsigned(v>>8&255),unsigned(v&255));}
bool subnetRange(uint32_t address,uint32_t mask,uint32_t& first,uint32_t& last){
  uint32_t host=~mask;if(!mask||(host&(host+1))||host<3||host>255)return false;
  first=(address&mask)+1;last=(address|host)-1;return true;
}
size_t portalDnsReply(const uint8_t* q,size_t n,uint8_t* out,size_t cap,uint32_t ip){
  if(!q||!out||q==out||n<17||n>512||(q[2]&0xf8)||q[4]||q[5]!=1||q[6]||q[7]||q[8]||q[9])return 0;
  size_t p=12,total=0;while(p<n&&q[p]){unsigned z=q[p++];if(z>63||z>n-p||total+z+1>253)return 0;total+=z+1;p+=z;}
  if(!total||p>=n||n-p<5)return 0;++p;
  if(q[p]||q[p+1]!=1||q[p+2]||q[p+3]!=1)return 0;p+=4;
  size_t size=p+16;if(size>cap)return 0;memcpy(out,q,p);out[2]=0x81;out[3]=0x80;out[6]=0;out[7]=1;memset(out+8,0,4);
  const uint8_t a[]={0xc0,0x0c,0,1,0,1,0,0,0,0,0,4};memcpy(out+p,a,sizeof(a));
  for(unsigned i=0;i<4;++i)out[p+12+i]=ip>>(24-i*8);return size;
}
}
