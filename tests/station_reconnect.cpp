#include "../main/station_reconnect.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
using wire::StationEvent;
using Policy=wire::StationReconnect;
using Action=Policy::Action;

int main(){
 // Short AP reboot regression: beacons/driver association remain up but no
 // control ACK arrives. One bad packet never resets a healthy association.
 wire::ControlLinkRecovery control;
 assert(!control.failed(0,true));assert(!control.failed(100,true));
 assert(!control.failed(200,true));assert(!control.failed(300,true));
 assert(!control.failed(14999,true));assert(control.failed(15000,true));
 assert(control.failures()==0 && control.failure_ms(15000)==0);
 // A failed peer remains rate-limited after recovery, even across Wi-Fi down.
 assert(!control.failed(15001,false));
 for(uint32_t t=15250;t<45000;t+=250)assert(!control.failed(t,true));
 assert(control.failed(45000,true));
 // A valid ACK resets the failure window, rather than just a TCP handshake.
 wire::ControlLinkRecovery intermittent;
 for(uint32_t t=0;t<14000;t+=1000)assert(!intermittent.failed(t,true));
 intermittent.healthy();
 assert(!intermittent.failed(15000,true));assert(intermittent.failures()==1);
 assert(intermittent.failure_ms(16000)==1000);
 assert(!intermittent.failed(29999,true));
 intermittent.healthy();assert(intermittent.failure_ms(40000)==0);
 // At least five failed protocol exchanges, not one isolated long timeout.
 wire::ControlLinkRecovery sparse;
 assert(!sparse.failed(0,true));assert(!sparse.failed(60000,true));
 assert(!sparse.failed(60001,true));assert(!sparse.failed(60002,true));
 assert(sparse.failed(60003,true));
 wire::ControlLinkRecovery offline;
 for(uint32_t t=0;t<60000;t+=1000)assert(!offline.failed(t,false));
 assert(offline.failures()==0 && offline.failure_ms(60000)==0);
 wire::ControlLinkRecovery control_wrap;
 assert(!control_wrap.failed(0xfffffff0u,true));
 for(uint32_t t=0;t<3;++t)assert(!control_wrap.failed(t,true));
 assert(!control_wrap.failed(14983,true));assert(control_wrap.failed(14984,true));
 // Event-loop ordering regression: CONNECTED's GOT_IP may be delivered only
 // after an already queued DISCONNECTED. It must not replace that mailbox
 // transition or leave the worker permanently "online" while unassociated.
 wire::StationEventGate gate;StationEvent pending=StationEvent::none;
 auto publish=[&](StationEvent event,bool matching_netif=true){
  if(!gate.accept(event,matching_netif))return false;
  pending=event;return true;
 };
 assert(!publish(StationEvent::got_ip)); // no association during initialization
 assert(publish(StationEvent::started));assert(publish(StationEvent::associated));
 assert(!publish(StationEvent::got_ip,false)); // another netif cannot arm Wi-Fi
 assert(publish(StationEvent::disconnected));
 assert(!publish(StationEvent::got_ip));assert(pending==StationEvent::disconnected);
 Policy ordered;ordered.event(StationEvent::started,0);
 assert(ordered.poll(0,false)==Action::connect);
 ordered.event(pending,10);
 assert(ordered.poll(259,false)==Action::none);assert(ordered.poll(260,false)==Action::connect);
 // A later real association accepts its IP notification and becomes usable.
 assert(publish(StationEvent::associated));assert(publish(StationEvent::got_ip));
 ordered.event(pending,300);assert(ordered.poll(20000,true)==Action::none);
 // Stop/start also invalidate any IP notification left from the prior link.
 assert(publish(StationEvent::stopped));assert(!publish(StationEvent::got_ip));
 assert(publish(StationEvent::started));assert(!publish(StationEvent::got_ip));
 assert(publish(StationEvent::associated));assert(publish(StationEvent::got_ip));
 // A synchronous connect error has no completion event: it must retry by time.
 Policy p;p.event(StationEvent::started,0);
 assert(p.poll(0,false)==Action::connect);p.connect_result(false,0);
 assert(p.poll(249,false)==Action::none);assert(p.poll(250,false)==Action::connect);
 p.connect_result(false,250);
 assert(p.poll(749,false)==Action::none);assert(p.poll(750,false)==Action::connect);
 // An accepted attempt is left alone while scanning/authenticating.
 p.connect_result(true,750);
 for(uint32_t t=850;t<15750;t+=100)assert(p.poll(t,false)==Action::none);
 assert(p.poll(15750,false)==Action::timeout);
 // Timeout's async disconnect does not postpone/double the planned retry.
 p.event(StationEvent::disconnected,15800);
 assert(p.poll(16749,false)==Action::none);assert(p.poll(16750,false)==Action::connect);
 // Ordinary AP loss schedules a new attempt, and GOT_IP resets the backoff.
 p.event(StationEvent::associated,17000);p.event(StationEvent::got_ip,17020);
 assert(p.poll(90000,true)==Action::none);
 p.event(StationEvent::disconnected,91000);
 assert(p.poll(91249,false)==Action::none);assert(p.poll(91250,false)==Action::connect);
 p.event(StationEvent::got_ip,91270);assert(p.poll(100000,true)==Action::none);
 // Readiness reconciliation also recovers a missing GOT_IP event.
 Policy reconciled;reconciled.event(StationEvent::started,0);
 assert(reconciled.poll(0,false)==Action::connect);
 reconciled.event(StationEvent::associated,100);
 assert(reconciled.poll(15100,false)==Action::timeout);
 reconciled.event(StationEvent::got_ip,15100);
 assert(reconciled.poll(20000,true)==Action::none);
 // A real association awaiting IP gets time to finish, without a new connect.
 Policy associated;associated.event(StationEvent::started,0);
 assert(associated.poll(0,false)==Action::connect);
 associated.event(StationEvent::associated,14900);
 assert(associated.poll(29899,false)==Action::none);
 assert(associated.poll(29900,false)==Action::timeout);
 // The worker queried an associated station: defer instead of disconnecting.
 associated.event(StationEvent::associated,29900);
 assert(associated.poll(30000,false)==Action::none);
 associated.event(StationEvent::got_ip,30100);
 assert(associated.poll(60000,true)==Action::none);
 // Persistent synchronous failure stays bounded at 4 seconds between retries.
 Policy failed;failed.event(StationEvent::started,0);uint32_t now=0;
 for(uint32_t delay:{250u,500u,1000u,2000u,4000u,4000u,4000u}){
  assert(failed.poll(now,false)==Action::connect);failed.connect_result(false,now);
  assert(failed.poll(now+delay-1,false)==Action::none);now+=delay;
 }
 // Stop suppresses attempts; start explicitly re-enables them.
 failed.event(StationEvent::stopped,now);assert(failed.poll(now+60000,false)==Action::none);
 failed.event(StationEvent::started,now+60000);assert(failed.poll(now+60000,false)==Action::connect);
 // Deadlines remain valid across millis() wrapping at 2^32.
 Policy wrap;wrap.event(StationEvent::started,0xfffffff0u);
 assert(wrap.poll(0xfffffff0u,false)==Action::connect);wrap.connect_result(false,0xfffffff0u);
 assert(wrap.poll(233,false)==Action::none);assert(wrap.poll(234,false)==Action::connect);
 puts("station reconnect policy: passed");
}
