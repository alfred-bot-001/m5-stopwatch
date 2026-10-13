#include "../main/radio_idle.hpp"
#include "../main/wire_protocol.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using wire::RadioIdle;

int main(){
 // Startup and every explicit interaction get five seconds at full speed.
 assert(!RadioIdle::saving(4999,0,false,true));
 assert(RadioIdle::saving(5000,0,false,true));
 assert(!RadioIdle::saving(5000,5000,false,true));
 assert(!RadioIdle::saving(9999,5000,false,true));
 assert(RadioIdle::saving(10000,5000,false,true));
 // A held modifier/key or live listening session stays awake indefinitely;
 // battery/level/session metadata and zero reports must not hold it awake.
 wire::Control state;
 state.battery_mv=4000;state.level=12000;state.session=12;
 assert(!wire::has_input(state));
 assert(RadioIdle::saving(60000,0,wire::has_input(state),true));
 for(unsigned index:{0u,2u,3u,7u}){
  state.keys[index]=1;assert(wire::has_input(state));
  assert(!RadioIdle::saving(60000,0,wire::has_input(state),true));
  state.keys[index]=0;
 }
 state.listening=1;assert(!RadioIdle::saving(60000,0,wire::has_input(state),true));
 state.listening=0;state.wheel=-1;assert(!RadioIdle::saving(60000,0,wire::has_input(state),true));
 state.wheel=0;
 // Link setup/recovery cannot enter modem sleep, even after a long idle.
 assert(!RadioIdle::saving(60000,0,false,false));
 // Idle traffic is 12.5 rather than 50 heartbeats/sec, retaining margin below
 // the existing receiver socket timeout and the 350 ms fail-safe input lease.
 assert(RadioIdle::interval(false)==20);assert(RadioIdle::interval(true)==80);
 assert(RadioIdle::idle_control_ms<150);
 assert(RadioIdle::idle_control_ms<wire::lease_ms);
 // Response latency consumes the period; it is never added to it. This is
 // important with modem sleep, which can defer receipt until the next DTIM.
 assert(RadioIdle::wait_ms(100,100,true)==80);
 assert(RadioIdle::wait_ms(115,100,true)==65);
 assert(RadioIdle::wait_ms(179,100,true)==1);
 assert(RadioIdle::wait_ms(180,100,true)==0);
 assert(RadioIdle::wait_ms(225,100,true)==0);
 assert(RadioIdle::wait_ms(115,100,false)==5);
 assert(RadioIdle::wait_ms(121,100,false)==0);
 // Both the idle delay and transmission deadline survive the millis wrap.
 uint32_t activity=0xfffffff0u;
 assert(RadioIdle::idle_ms(10,activity,false)==26);
 assert(!RadioIdle::saving(activity+4999,activity,false,true));
 assert(RadioIdle::saving(activity+5000,activity,false,true));
 assert(RadioIdle::wait_ms(10,activity,true)==54);
 assert(RadioIdle::wait_ms(64,activity,true)==0);
 assert(RadioIdle::idle_ms(60000,0,true)==0);
 // First PTT audio is retained until NONE succeeds. Release, link loss, or
 // a new token/session cancel immediately, even if wake finishes afterwards.
 using Action=wire::AudioWake::Action;
 assert(wire::AudioWake::poll(100,100,true,false)==Action::wait);
 assert(wire::AudioWake::poll(103,100,true,true)==Action::send);
 assert(wire::AudioWake::poll(103,100,false,false)==Action::discard);
 assert(wire::AudioWake::poll(104,100,false,true)==Action::discard);
 assert(wire::AudioWake::poll(299,100,true,false)==Action::wait);
 assert(wire::AudioWake::poll(300,100,true,false)==Action::discard);
 assert(wire::AudioWake::poll(300,100,true,true)==Action::discard);
 assert(wire::AudioWake::poll(10,activity,true,false)==Action::wait);
 assert(wire::AudioWake::poll(activity+200,activity,true,true)==Action::discard);
 puts("radio idle policy: passed");
}
