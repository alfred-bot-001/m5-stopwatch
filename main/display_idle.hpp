#pragma once
#include <cstdint>

#ifndef VIBE_DISPLAY_IDLE_MS
#define VIBE_DISPLAY_IDLE_MS 1800000u
#endif
static constexpr uint32_t kDisplayIdleTimeoutMs=VIBE_DISPLAY_IDLE_MS;
static_assert(kDisplayIdleTimeoutMs>0 && kDisplayIdleTimeoutMs<0x80000000u,
              "Display timeout must be positive and fit the wrap-safe interval");

// Main-task owned. Only real interaction/data activity is supplied here;
// connection heartbeats, USB polling and diagnostic reads are not activity.
class DisplayIdle {
 public:
  explicit DisplayIdle(uint32_t now,uint32_t timeout=kDisplayIdleTimeoutMs)
      :last_activity_(now),timeout_(timeout){}
  bool update(uint32_t now,bool active){
    if(active){last_activity_=now;awake_=true;}
    else if(awake_ && uint32_t(now-last_activity_)>=timeout_)awake_=false;
    return awake_;
  }
  bool awake()const{return awake_;}
  uint32_t idle_ms(uint32_t now)const{return uint32_t(now-last_activity_);}
 private:
  uint32_t last_activity_,timeout_;
  bool awake_=true;
};
