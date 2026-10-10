#pragma once
#include <cmath>
#include <cstdint>

// Voltage-based estimate, matching the factory's 3.3 V empty / 4.2 V full
// range. This is not a fuel gauge: load and charging affect battery voltage.
// Call sample() on every polling cycle, including failed reads, so stale
// measurements expire even while the display is asleep.
class BatteryLevel {
public:
 static constexpr uint32_t stale_ms=5000;
 static constexpr float time_constant_ms=8000.f;

 void sample(uint32_t now,bool valid,unsigned millivolts) {
  const uint32_t elapsed=uint32_t(now-last_valid_);
  const bool fresh=known_ && elapsed<stale_ms;
  if(!valid || millivolts<2000 || millivolts>5000){
   if(!fresh){known_=false;filtered_=0.f;percent_=-1;}
   return;
  }
  if(!fresh){
   filtered_=float(millivolts);
   percent_=rounded_percent(filtered_);
  } else {
   // Time-based smoothing behaves consistently if polling is delayed or its
   // frequency changes. Unsigned subtraction also handles clock rollover.
   filtered_+=(float(millivolts)-filtered_)*(1.f-std::exp(-float(elapsed)/time_constant_ms));
   const float estimate=voltage_percent(filtered_);
   // About 1% hysteresis between the rising/falling rounding boundaries.
   // The 0.01% tolerance lets a constant integer percentage converge despite
   // asymptotic filtering, rather than sticking one point below/above it.
   if(std::fabs(estimate-float(percent_))>=0.99f)percent_=rounded_percent(filtered_);
  }
  known_=true;last_valid_=now;
 }

 int percent() const {return percent_;} // -1 when never sampled or stale.
 unsigned filtered_mv() const {return known_?unsigned(filtered_+0.5f):0u;}

private:
 bool known_=false;
 uint32_t last_valid_=0;
 float filtered_=0.f;
 int percent_=-1;
 static float voltage_percent(float millivolts) {
  return std::fmax(0.f,std::fmin(100.f,(millivolts-3300.f)/9.f));
 }
 static int rounded_percent(float millivolts) {return int(voltage_percent(millivolts)+0.5f);}
};
