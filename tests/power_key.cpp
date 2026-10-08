#include "../main/power_key.hpp"
#include <cassert>
#include <cstdio>

int main() {
 // Cover every prior state, including the dangerous download-lock bit and
 // timing combinations. The guard must never change these unrelated settings.
 for(unsigned before=0;before<=255;++before){
  const auto after=guarded_power_key_config(uint8_t(before));
  assert((after & 0x80)==(before & 0x80)); // Download recovery stays unchanged.
  assert((after & 0x60)==(before & 0x60)); // Double-click timing stays unchanged.
  assert((after & 0x06)==(before & 0x06)); // Single-click timing stays unchanged.
  assert((after & 0x18)==0x18); // Four-second hold.
  assert((after & 0x01)==0x01); // No short-click reset.
  assert(guarded_power_key_config(after)==after);
 }
 assert(guarded_power_key_config(0x2a)==0x3b); // Observed factory configuration.
 assert(guarded_power_key_config(0x2f)==0x3f); // Earlier observed configuration.
 std::puts("power key guard: recovery lock and click timing preserved for all register states");
}
