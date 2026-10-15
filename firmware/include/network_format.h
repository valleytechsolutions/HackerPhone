#pragma once
#include <stdint.h>
#include <stddef.h>
namespace hackerphone {
bool parseIpv4(const char* text,uint32_t& hostOrder);
void formatIpv4(uint32_t hostOrder,char out[16]);
bool subnetRange(uint32_t address,uint32_t mask,uint32_t& first,uint32_t& last);
size_t portalDnsReply(const uint8_t* query,size_t length,uint8_t* output,size_t capacity,uint32_t hostOrderIp);
enum class HttpParse:uint8_t {Incomplete,Ready,Invalid,TooLarge};
struct PortalRequest {bool post=false;size_t bodyOffset=0,bodySize=0;};
HttpParse parsePortalRequest(const char* bytes,size_t length,PortalRequest& output);
}
