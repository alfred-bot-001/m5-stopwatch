#include "../main/pmic_diagnostics.hpp"
#include <cassert>
#include <cstdio>

using namespace vibe_pmic_diag;
static bool same(const Snapshot& a, const Snapshot& b) { return encode(a) == encode(b); }

int main() {
 constexpr std::uint8_t known[] = {'1','2','3','4','5','6','7','8','9'};
 assert(crc16(known, sizeof(known)) == 0x29b1);
 assert(slot_register(0) == 0xa0 && slot_register(1) == 0xb0);
 assert(flags(Intent::power_reset, true, true, true, true) == 0x3e);
 const Snapshot source{0x1234, 0x89abcdef, 0x81, 0x3b, 0x10, 0x0f,
  flags(Intent::download, true, true, false, true), 0x05};
 const auto encoded = encode(source);
 assert(encoded[2] == 0x34 && encoded[3] == 0x12);
 assert(encoded[4] == 0xef && encoded[7] == 0x89);
 Snapshot decoded;
 assert(decode(encoded, decoded) && same(decoded, source));

 // Every single-bit corruption is detected, including both CRC bytes.
 for (std::size_t byte = 0; byte < kSlotSize; ++byte) for (unsigned bit = 0; bit < 8; ++bit) {
  auto corrupt = encoded; corrupt[byte] ^= std::uint8_t(1u << bit);
  Snapshot untouched = source;
  assert(!decode(corrupt, untouched) && same(untouched, source));
 }
 for (std::size_t length = 0; length < kSlotSize; ++length)
  assert(!decode(encoded.data(), length, decoded));
 assert(!decode(encoded.data(), kSlotSize + 1, decoded));
 assert(!decode(nullptr, kSlotSize, decoded));
 Slot blank{};
 assert(!decode(blank, decoded)); blank.fill(0xff); assert(!decode(blank, decoded));
 auto other_version = encoded; other_version[1] = 2;
 const auto version_crc = crc16(other_version.data(), 14);
 other_version[14] = std::uint8_t(version_crc); other_version[15] = std::uint8_t(version_crc >> 8);
 assert(!decode(other_version, decoded));

 Slots slots;
 assert(!slots.newest().valid);
 assert(!slots.load(2, encoded) && !slots.load(0, nullptr, kSlotSize));
 auto first = slots.prepare(source);
 assert(first.index == 0 && first.snapshot.sequence == 1 && !slots.newest().valid);
 assert(!slots.confirm(first, first.raw.data(), 8));
 auto corrupt = first.raw; corrupt[8] ^= 1;
 assert(!slots.confirm(first, corrupt) && !slots.newest().valid);
 assert(slots.confirm(first, first.raw));
 assert(slots.newest().valid && slots.newest().index == 0 && slots.newest().snapshot.sequence == 1);
 auto second = slots.prepare(source); second.snapshot.uptime_ms += 1000;
 second.raw = encode(second.snapshot);
 assert(second.index == 1 && second.snapshot.sequence == 2);
 assert(!slots.confirm(first, first.raw)); // Old completion must not advance state.
 assert(slots.confirm(second, second.raw));
 assert(slots.newest().index == 1 && slots.newest().snapshot.sequence == 2);
 assert(!slots.load(1, second.raw.data(), 15));
 assert(slots.newest().snapshot.sequence == 2); // Failed read keeps complete observation.

 // Simulate an interrupted sequential I2C write into the older slot, then a
 // reboot. Every prefix either leaves an older valid record, is invalid, or
 // contains the fully valid new record. Never select corrupt data over latest.
 auto third = slots.prepare(source);
 third.snapshot.uptime_ms = 0x10203040; third.snapshot.button = 0x01;
 third.raw = encode(third.snapshot);
 for (std::size_t prefix = 0; prefix <= kSlotSize; ++prefix) {
  auto torn = first.raw;
  for (std::size_t i = 0; i < prefix; ++i) torn[i] = third.raw[i];
  Slots recovered;
  assert(recovered.load(0, torn) && recovered.load(1, second.raw));
  const auto selected = recovered.newest();
  assert(selected.valid);
  assert(same(selected.snapshot, second.snapshot) || same(selected.snapshot, third.snapshot));
  if (prefix == kSlotSize) assert(same(selected.snapshot, third.snapshot));
 }
 // No usable slots, one usable slot, equal sequence, and wrap-around ordering.
 slots.clear(); assert(!slots.newest().valid);
 assert(slots.load(0, blank) && !slots.newest().valid);
 assert(slots.load(1, second.raw) && slots.newest().index == 1);
 assert(slots.load(0, second.raw) && slots.newest().index == 0);
 Snapshot before_wrap = source; before_wrap.sequence = 65535;
 Snapshot after_wrap = source; after_wrap.sequence = 0;
 assert(slots.load(0, encode(before_wrap)) && slots.load(1, encode(after_wrap)));
 assert(slots.newest().index == 1 && slots.prepare(source).snapshot.sequence == 1);
 slots.clear(); assert(slots.load(0, encode(before_wrap)));
 const auto wrapped = slots.prepare(source);
 assert(wrapped.index == 1 && wrapped.snapshot.sequence == 0 && slots.confirm(wrapped, wrapped.raw));
 assert(newer(0, 65535) && !newer(65535, 0));
 assert(!newer(0, 0) && !newer(0x8000, 0) && !newer(0, 0x8000));

 // Exercise two full sequence wraps and periodic reconstruction from the raw
 // hardware slots, while only committing confirmed readbacks.
 Slots running;
 std::array<Slot, kSlotCount> hardware{};
 for (std::uint32_t n = 0; n < 131075; ++n) {
  Snapshot value = source; value.uptime_ms = n * 1000u;
  const auto write = running.prepare(value);
  hardware[write.index] = write.raw;
  assert(running.confirm(write, hardware[write.index]));
  if (n % 29 == 0) {
   Slots reboot;
   assert(reboot.load(0, hardware[0]) && reboot.load(1, hardware[1]));
   running = reboot;
  }
  const auto latest = running.newest();
  assert(latest.valid && latest.snapshot.sequence == std::uint16_t(n + 1));
  assert(latest.snapshot.uptime_ms == n * 1000u);
 }
 std::puts("PMIC diagnostics: codec, CRC, torn writes, readback, sequence wraps passed");
}
