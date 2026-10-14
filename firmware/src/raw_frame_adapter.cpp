// ESP32 Marauder compatibility hook (MIT reference retained in CREDITS.md).
// Pinned to the ESP32-S3 / IDF 5.5.5 driver. The linker chooses this definition
// before the archive's definition, using Marauder's documented -zmuldefs flag.
#include <stdint.h>
#include "attack_service.h"
extern "C" int ieee80211_raw_frame_sanity_check(int32_t marker,int32_t,int32_t) {
  return marker==31337?1:0;
}
namespace hackerphone {
bool rawFrameAdapterLinked(){return ieee80211_raw_frame_sanity_check(31337,0,0)==1;}
}
