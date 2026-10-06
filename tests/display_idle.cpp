#include "../main/display_idle.hpp"
#include "../main/wire_protocol.hpp"
#include <cassert>
#include <cstdio>

int main(){
 static_assert(kDisplayIdleTimeoutMs==30u*60u*1000u,"Production default is 30 minutes");
 DisplayIdle idle(100);
 assert(idle.update(100+kDisplayIdleTimeoutMs-1,false));
 assert(!idle.update(100+kDisplayIdleTimeoutMs,false));
 assert(!idle.update(100+kDisplayIdleTimeoutMs+1000,false));
 assert(idle.update(100+kDisplayIdleTimeoutMs+1001,true));
 assert(idle.idle_ms(100+kDisplayIdleTimeoutMs+1001)==0);
 // A held button/session prevents blanking; release starts the full interval.
 DisplayIdle held(0,1000);
 for(uint32_t now=0;now<10000;now+=500)assert(held.update(now,true));
 assert(held.update(10499,false));assert(!held.update(10500,false));
 // A real event at the exact expiry boundary wins, and is not swallowed.
 DisplayIdle boundary(0,1000);assert(boundary.update(1000,true));
 assert(boundary.update(1999,false));assert(!boundary.update(2000,false));
 // Uptime wrap must not prevent sleep, or spuriously wake an already blank display.
 DisplayIdle wrap(0xfffffff0u,1000);
 assert(wrap.update(983,false));assert(!wrap.update(984,false));
 assert(!wrap.update(0xfffffff0u,false));assert(!wrap.update(4,false));
 assert(wrap.update(5,true));assert(wrap.awake());
 // Wire metadata / ACK polling / all-zero PCM are not display activity.
 wire::Control previous,current;
 current.battery_mv=4120;current.level=200;
 assert(!wire::control_activity(current,previous));
 DisplayIdle receiver(0,1000);
 for(uint32_t now=0;now<1000;now+=20){
  current.battery_mv=4100+now%20;
  assert(receiver.update(now,wire::control_activity(current,previous)));previous=current;
 }
 assert(!receiver.update(1000,wire::control_activity(current,previous)));
 current.keys[2]=0x28;assert(receiver.update(1001,wire::control_activity(current,previous)));
 previous=current;current.keys[2]=0;
 assert(wire::control_activity(current,previous)); // Release is real activity.
 previous=current;assert(!wire::control_activity(current,previous));
 current.wheel=-1;assert(wire::control_activity(current,previous));
 current.wheel=0;current.listening=1;current.keys[0]=0x40;
 assert(wire::control_activity(current,previous));
 puts("display idle and meaningful control activity: passed");
}
