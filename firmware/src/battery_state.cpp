#include "battery_state.h"

namespace hackerphone {
uint8_t batteryPercent(uint16_t mv) {
  struct Point {uint16_t mv;uint8_t percent;};
  static constexpr Point curve[]={{3300,0},{3500,5},{3600,10},{3700,20},{3800,45},{3900,65},{4000,80},{4100,90},{4200,100}};
  if(mv<=curve[0].mv)return 0;
  for(unsigned i=1;i<sizeof(curve)/sizeof(curve[0]);++i){
    if(mv<=curve[i].mv){const auto& a=curve[i-1];const auto& b=curve[i];return a.percent+uint32_t(mv-a.mv)*(b.percent-a.percent)/(b.mv-a.mv);}
  }
  return 100;
}
void BatteryGauge::update(uint16_t mv,bool valid,bool host,bool charging,uint32_t now) {
  auto previous=state_;state_={};sampled_=true;sampledAt_=now;
  state_.millivolts=valid?mv:0;
  // This board measures a shared supply rail, which rises on USB power.
  if(charging){state_.power=BatteryPower::Charging;return;}
  if(host||(valid&&mv>4500)){state_.power=BatteryPower::Usb;return;}
  if(!valid||mv<2800||mv>4300)return;
  state_.power=BatteryPower::Battery;
  if(previous.power==BatteryPower::Battery)state_.millivolts=(uint32_t(previous.millivolts)*3+mv+2)/4;
  state_.percent=batteryPercent(state_.millivolts);
  state_.low=state_.percent<=10;
}
BatteryStatus BatteryGauge::status(uint32_t now) const {
  if(!sampled_||uint32_t(now-sampledAt_)>10000)return {};
  return state_;
}
}
