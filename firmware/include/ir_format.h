#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hackerphone {
struct IrSignal {
  static constexpr size_t Capacity=1024;
  char name[48]{};
  uint32_t frequency=38000;
  float duty=0.33f;
  uint16_t count=0;
  uint32_t durations[Capacity]{}; // microseconds, alternating mark / space
};
class IrInput {public:virtual ~IrInput()=default;virtual int read()=0;};
enum class IrReadResult {Ready,End,Invalid,Unsupported};
// One record at a time. Large data lines are parsed directly into the fixed
// signal buffer; an entire remote library is never read into RAM.
class IrReader {
 public:
  void reset(){header_=version_=false;}
  IrReadResult next(IrInput& input,IrSignal& signal,char* error,size_t capacity);
 private:
  bool header_=false,version_=false;
};
bool validIrSignal(const IrSignal& signal);
bool irEncodeParsed(IrSignal& signal,const char* protocol,uint32_t address,uint32_t command);
bool parseIrHex32(const char* text,uint32_t& value);
bool validIrLibraryPath(const char* path);
}
