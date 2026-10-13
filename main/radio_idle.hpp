#pragma once
#include <cstdint>

namespace wire {
// The original receivers close a control socket after 150 ms without data.
// Keep the idle heartbeat shorter than that, including room for scheduling
// and packet delivery, rather than extending the receiver's input lease.
struct RadioIdle {
 static constexpr uint32_t settle_ms=5000,active_control_ms=20,idle_control_ms=80;
 static uint32_t idle_ms(uint32_t now,uint32_t activity,bool held){
  return held?0:uint32_t(now-activity);
 }
 static bool saving(uint32_t now,uint32_t activity,bool held,bool connected){
  return connected && idle_ms(now,activity,held)>=settle_ms;
 }
 static uint32_t interval(bool saving){return saving?idle_control_ms:active_control_ms;}
 // Count from transmission, not from ACK reception. A delayed ACK therefore
 // consumes the existing heartbeat budget instead of adding another 80 ms.
 static uint32_t wait_ms(uint32_t now,uint32_t sent_at,bool saving){
  uint32_t elapsed=uint32_t(now-sent_at),period=interval(saving);
  return elapsed<period?period-elapsed:0;
 }
};

struct AudioWake {
 enum class Action { wait,send,discard };
 static constexpr uint32_t timeout_ms=200;
 static Action poll(uint32_t now,uint32_t captured_at,bool session_valid,bool awake){
  if(!session_valid || uint32_t(now-captured_at)>=timeout_ms)return Action::discard;
  return awake?Action::send:Action::wait;
 }
};
}
