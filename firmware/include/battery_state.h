#pragma once
#include <stdint.h>

namespace hackerphone {
enum class BatteryPower : uint8_t { Unknown, Battery, Usb, Charging };
struct BatteryStatus {
  BatteryPower power=BatteryPower::Unknown;
  uint16_t millivolts=0;
  uint8_t percent=0;
  bool low=false;
};

// Approximate single-cell Li-ion state of charge, not a calibrated fuel gauge.
uint8_t batteryPercent(uint16_t millivolts);
class BatteryGauge {
public:
  void update(uint16_t railMv,bool valid,bool usbHost,bool charging,uint32_t now);
  BatteryStatus status(uint32_t now) const;
private:
  BatteryStatus state_;
  uint32_t sampledAt_=0;
  bool sampled_=false;
};
}
