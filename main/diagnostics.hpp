#pragma once
#include <cstdint>

// BTN_Event is a read-to-clear latch: retain every observed event, including
// events read in a poll whose other PMIC registers subsequently fail.
struct PowerButtonHistory {
 uint32_t events=0,last_event_ms=0,held_ms=0,max_held_ms=0;
 uint32_t last_sample_ms=0;
 bool held=false;
 void sample(bool valid,uint8_t status,uint32_t now) {
  if(!valid){held=false;held_ms=0;return;}
  if(status&0x80){if(events!=UINT32_MAX)++events;last_event_ms=now;}
  if(status&1){
   if(held){uint32_t elapsed=now-last_sample_ms;
    held_ms=elapsed>UINT32_MAX-held_ms?UINT32_MAX:held_ms+elapsed;
   }else held_ms=0;
   if(held_ms>max_held_ms)max_held_ms=held_ms;
   held=true;
  }else{held=false;held_ms=0;}
  last_sample_ms=now;
 }
};

// Plain words, with no constructor: the instance in RTC_NOINIT must survive
// eligible ESP resets without startup initialization. Power loss may erase it.
struct DiagnosticSnapshot {
 uint32_t magic,version,uptime_ms,pmic_state,flags;
 uint32_t events,last_event_ms,held_ms,max_held_ms,intent;
 uint32_t reset_raw,strap_raw,usb_events,checksum;
};
static constexpr uint32_t diagnostic_magic=0x56494245,diagnostic_version=1;
inline uint32_t diagnostic_checksum(const DiagnosticSnapshot &s) {
 const uint32_t words[]={s.magic,s.version,s.uptime_ms,s.pmic_state,s.flags,
  s.events,s.last_event_ms,s.held_ms,s.max_held_ms,s.intent,s.reset_raw,s.strap_raw,s.usb_events};
 uint32_t hash=2166136261u;
 for(uint32_t word:words)for(unsigned i=0;i<4;i++){hash^=(word>>(8*i))&255u;hash*=16777619u;}
 return hash;
}
inline bool diagnostic_valid(const DiagnosticSnapshot &s) {
 return s.magic==diagnostic_magic && s.version==diagnostic_version
  && s.checksum==diagnostic_checksum(s);
}
inline DiagnosticSnapshot diagnostic_read(const volatile DiagnosticSnapshot &s) {
 return {s.magic,s.version,s.uptime_ms,s.pmic_state,s.flags,s.events,
  s.last_event_ms,s.held_ms,s.max_held_ms,s.intent,s.reset_raw,s.strap_raw,s.usb_events,s.checksum};
}
// Caller serializes writers. Invalidate first and commit the magic last so a
// reset partway through the write cannot present an unfinished valid record.
inline void diagnostic_store(volatile DiagnosticSnapshot &s,DiagnosticSnapshot value) {
 value.magic=diagnostic_magic;value.version=diagnostic_version;
 value.checksum=diagnostic_checksum(value);
 s.magic=0;s.version=value.version;s.uptime_ms=value.uptime_ms;
 s.pmic_state=value.pmic_state;s.flags=value.flags;s.events=value.events;
 s.last_event_ms=value.last_event_ms;s.held_ms=value.held_ms;s.max_held_ms=value.max_held_ms;
 s.intent=value.intent;s.reset_raw=value.reset_raw;s.strap_raw=value.strap_raw;
 s.usb_events=value.usb_events;s.checksum=value.checksum;s.magic=value.magic;
}
