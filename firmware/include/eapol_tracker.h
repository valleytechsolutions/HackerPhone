#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hackerphone {
struct EapolKey {uint8_t ap[6]{},client[6]{},nonce[32]{};uint8_t message=0;uint64_t replay=0;bool pmkid=false;};
bool parseEapolKey(const uint8_t* frame,size_t length,EapolKey& key);
class EapolTracker {
 public:
  uint32_t messages[4]{},complete=0,pmkids=0;
  void observe(const uint8_t* frame,size_t length,uint32_t now);
 private:
  struct Exchange {bool used=false,counted=false;uint8_t ap[6]{},client[6]{},nonce[32]{},seen=0;uint64_t firstReplay=0,thirdReplay=0;uint32_t at=0;} sessions_[8]{};
};
}
