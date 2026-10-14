#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hackerphone {
// Paths accepted from a future file browser stay under the app's own SD directory.
bool safeRelativePath(const char* path);
size_t csvField(char* out,size_t cap,const char* text);
void pcapHeader(uint8_t out[24]);
bool pcapRecord(uint8_t out[16],uint64_t unixMicros,uint32_t captured,uint32_t original);
}
