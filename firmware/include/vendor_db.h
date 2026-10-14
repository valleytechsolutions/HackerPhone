#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hackerphone {
// Independently defined, fixed-record SD format. No upstream application data/code.
struct VendorReader {virtual ~VendorReader()=default;virtual size_t size()const=0;virtual bool read(size_t offset,void* data,size_t length)=0;};
class VendorDb {
 public:
  static constexpr unsigned HeaderSize=16,RecordSize=52;
  bool open(VendorReader& reader);
  bool lookup(const uint8_t mac[6],bool ble,uint8_t addressType,char out[48]);
  uint32_t count()const{return count_;}
 private: VendorReader* reader_=nullptr;uint32_t count_=0;
};
const char* addressKind(const uint8_t mac[6],bool ble,uint8_t type);
}
