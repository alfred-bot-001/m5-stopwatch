#include "../main/power_key.hpp"
#include <cassert>
#include <cstdio>

int main() {
 // Cover every prior state in both opt-in modes. Unrelated timing settings
 // must survive, and disabling the option must never request a PMIC unlock.
 for(unsigned before=0;before<=255;++before){
  assert(guarded_power_key_config(uint8_t(before))==
         guarded_power_key_config(uint8_t(before),false));
  for(unsigned mode=0;mode<2;++mode){
   const bool lock_download=mode!=0;
   const auto after=guarded_power_key_config(uint8_t(before),lock_download);
   assert((after & 0x80)==(lock_download ? 0x80 : (before & 0x80)));
   assert((after & 0x60)==(before & 0x60)); // Double-click timing unchanged.
   assert((after & 0x06)==(before & 0x06)); // Single-click timing unchanged.
   assert((after & 0x18)==0x18); // Four-second hold.
   assert((after & 0x01)==0x01); // No short-click reset.
   assert(guarded_power_key_config(after,lock_download)==after);
   assert(guarded_power_key_config(after,false)==after); // No software unlock.
  }
 }
 assert(guarded_power_key_config(0x2a)==0x3b); // Observed factory configuration.
 assert(guarded_power_key_config(0x2f)==0x3f); // Earlier observed configuration.
 assert(guarded_power_key_config(0x2a,true)==0xbb); // Explicit lock request.
 assert(guarded_power_key_config(0xbb,false)==0xbb); // Requires PMIC power cycle.
 std::puts("power key guard: all 256 states x both lock modes passed; timings preserved, no unlock");
}
