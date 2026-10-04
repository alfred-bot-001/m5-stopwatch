#include "../main/diagnostics.hpp"
#include <cassert>
int main() {
 PowerButtonHistory b;
 b.sample(true,0x80,100); // The press was already released between polls.
 b.sample(true,0,350);
 assert(b.events==1 && b.last_event_ms==100 && b.held_ms==0);
 b.sample(true,0x81,500);b.sample(true,1,750);b.sample(true,1,1000);
 assert(b.events==2 && b.held_ms==500 && b.max_held_ms==500);
 b.sample(false,0,1250);b.sample(true,1,1500);
 assert(b.held_ms==0 && b.max_held_ms==500); // No invented duration through failed reads.
 b.sample(true,0,1750);b.sample(true,1,UINT32_MAX-99);b.sample(true,1,150);
 assert(b.held_ms==250); // Millisecond rollover.
 b.events=UINT32_MAX;b.held_ms=UINT32_MAX-10;b.sample(true,0x81,400);
 assert(b.events==UINT32_MAX && b.held_ms==UINT32_MAX && b.max_held_ms==UINT32_MAX);
 volatile DiagnosticSnapshot retained{};
 DiagnosticSnapshot value{0,0,123456,0x1c0f2f80,1,3,123000,250,500,2,0x80,8,0x01000201,0};
 diagnostic_store(retained,value);
 DiagnosticSnapshot valid=diagnostic_read(retained);
 assert(diagnostic_valid(valid) && valid.uptime_ms==123456 && valid.intent==2);
 // Every individual bit in the persisted record is covered by validation.
 for(unsigned byte=0;byte<sizeof(valid);byte++)for(unsigned bit=0;bit<8;bit++){
  DiagnosticSnapshot corrupt=valid;
  reinterpret_cast<unsigned char*>(&corrupt)[byte]^=1u<<bit;
  assert(!diagnostic_valid(corrupt));
 }
 retained.magic=0; // Simulate interruption before the commit marker.
 assert(!diagnostic_valid(diagnostic_read(retained)));
 assert(!diagnostic_valid(DiagnosticSnapshot{}));
}
