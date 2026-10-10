#include "../main/battery_level.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

static void settle(BatteryLevel &level,uint32_t start,unsigned mv,uint32_t duration=90000) {
 for(uint32_t elapsed=250;elapsed<=duration;elapsed+=250)level.sample(start+elapsed,true,mv);
}

int main() {
 BatteryLevel never;
 assert(never.percent()==-1&&never.filtered_mv()==0);
 never.sample(0,false,3750);assert(never.percent()==-1);
 // First valid read is displayed immediately, with clamped empty/full values.
 const unsigned mv[]={2000,3200,3300,3525,3750,3975,4200,4300,5000};
 const int pc[]={0,0,0,25,50,75,100,100,100};
 for(unsigned i=0;i<sizeof(mv)/sizeof(mv[0]);++i){
  BatteryLevel level;level.sample(0,true,mv[i]);
  assert(level.percent()==pc[i]&&level.filtered_mv()==mv[i]);
 }
 // Poll frequency does not alter the 8-second filter time constant.
 BatteryLevel fast,slow;fast.sample(0,true,3300);slow.sample(0,true,3300);
 for(uint32_t t=25;t<=8000;t+=25)fast.sample(t,true,4200);
 for(uint32_t t=1000;t<=8000;t+=1000)slow.sample(t,true,4200);
 const float expected=4200.f-900.f*std::exp(-1.f);
 assert(std::fabs(float(fast.filtered_mv())-expected)<1.f);
 assert(std::fabs(float(slow.filtered_mv())-expected)<1.f);
 assert(fast.percent()==slow.percent());
 // Both charge and discharge converge, including exact integer endpoints.
 BatteryLevel range;range.sample(0,true,3300);
 settle(range,0,4200);assert(range.percent()==100&&range.filtered_mv()==4200);
 settle(range,90000,3750);assert(range.percent()==50&&range.filtered_mv()==3750);
 settle(range,180000,3300);assert(range.percent()==0&&range.filtered_mv()==3300);
 // One short speaking/Wi-Fi load pulse must not make the indicator plunge.
 BatteryLevel pulse;pulse.sample(0,true,3750);pulse.sample(250,true,3450);
 assert(pulse.percent()>=49&&pulse.filtered_mv()>=3740);
 settle(pulse,250,3750);assert(pulse.percent()==50&&pulse.filtered_mv()==3750);
 // Noise straddling a rounding boundary cannot alternate adjacent pixels.
 BatteryLevel noise;noise.sample(0,true,3754);assert(noise.percent()==50);
 for(uint32_t t=250;t<=60000;t+=250){
  noise.sample(t,true,t%500?3753:3756);assert(noise.percent()==50);
 }
 settle(noise,60000,3762);assert(noise.percent()==51);
 for(uint32_t t=150250;t<=180000;t+=250){
  noise.sample(t,true,t%500?3753:3756);assert(noise.percent()==51);
 }
 settle(noise,180000,3750);assert(noise.percent()==50);
 // Repeated calls in the same millisecond do not advance the filter.
 BatteryLevel same;same.sample(12,true,3750);
 for(unsigned i=0;i<100;++i)same.sample(12,true,4200);
 assert(same.filtered_mv()==3750&&same.percent()==50);
 // Bad reads retain the most recent value briefly, then become unknown.
 BatteryLevel fault;fault.sample(100,true,3750);
 fault.sample(350,false,0);assert(fault.percent()==50&&fault.filtered_mv()==3750);
 fault.sample(600,true,1999);assert(fault.percent()==50);
 fault.sample(850,true,5001);assert(fault.percent()==50);
 fault.sample(1100,true,std::numeric_limits<unsigned>::max());assert(fault.percent()==50);
 fault.sample(5099,false,3750);assert(fault.percent()==50);
 fault.sample(5100,false,3750);assert(fault.percent()==-1&&fault.filtered_mv()==0);
 fault.sample(5350,true,4200);assert(fault.percent()==100&&fault.filtered_mv()==4200);
 // Recovery after a polling gap also starts fresh without an explicit failure.
 fault.sample(10350,true,3300);assert(fault.percent()==0&&fault.filtered_mv()==3300);
 // An invalid startup voltage cannot contaminate subsequent filtering.
 BatteryLevel invalid;invalid.sample(0,true,0);invalid.sample(250,true,5001);
 assert(invalid.percent()==-1&&invalid.filtered_mv()==0);
 invalid.sample(500,true,3975);assert(invalid.percent()==75);
 // Rollover has the same filtering, expiration and recovery as ordinary time.
 BatteryLevel normal,wrap;
 const uint32_t offset=0xfffff000u;
 for(uint32_t t=0;t<=30000;t+=250){
  const bool valid=t<10000||t>=17000;
  const unsigned voltage=t<17000?3750:4200;
  normal.sample(t,valid,voltage);wrap.sample(offset+t,valid,voltage);
  assert(normal.percent()==wrap.percent()&&normal.filtered_mv()==wrap.filtered_mv());
 }
 std::puts("battery level: endpoints, smoothing, charge/discharge, hysteresis, faults, recovery and clock wrap passed");
}
