#pragma once
#include <cstdint>

// Keep rendering animation through its final resting frame. Status changes are
// visible immediately, but a static frame never needs a periodic refresh.
class RenderIdle {
public:
 bool due(uint32_t now,uint64_t state,bool animating,bool wake) const {
  if(!valid_ || wake || state!=state_)return true;
  return (animating || animated_) && uint32_t(now-last_)>=33u;
 }
 void painted(uint32_t now,uint64_t state,bool animating) {
  valid_=true;last_=now;state_=state;animated_=animating;
 }
private:
 bool valid_=false,animated_=false;
 uint32_t last_=0;
 uint64_t state_=0;
};
