#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// M5PM1 RTC_MEM, 0xa0..0xbf. This memory outlives an ESP power/reset event,
// but does not survive the PMIC itself losing power. No I2C or allocation here.
namespace vibe_pmic_diag {
constexpr std::uint8_t kBaseRegister = 0xa0;
constexpr std::size_t kSlotSize = 16, kSlotCount = 2;
constexpr std::uint8_t kMagic = 0xd7, kVersion = 1;
using Slot = std::array<std::uint8_t, kSlotSize>;

enum class Intent : std::uint8_t { none = 0, download = 1, power_reset = 2 };
constexpr std::uint8_t kIntentMask = 0x03, kGpio0 = 0x04, kUsb = 0x08,
                       kYellow = 0x10, kScreen = 0x20;
constexpr std::uint8_t flags(Intent intent, bool gpio0, bool usb, bool yellow, bool screen) {
 return (static_cast<std::uint8_t>(intent) & kIntentMask)
  | (gpio0 ? kGpio0 : 0) | (usb ? kUsb : 0)
  | (yellow ? kYellow : 0) | (screen ? kScreen : 0);
}

struct Snapshot {
 std::uint16_t sequence = 0;
 std::uint32_t uptime_ms = 0;
 std::uint8_t button = 0, config = 0, wake = 0, power = 0, flags = 0, irq3 = 0;
};

// CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xffff, no reflection/xorout.
inline std::uint16_t crc16(const std::uint8_t* bytes, std::size_t length) {
 std::uint16_t crc = 0xffff;
 for (std::size_t i = 0; i < length; ++i) {
  crc ^= std::uint16_t(bytes[i]) << 8;
  for (unsigned bit = 0; bit < 8; ++bit)
   crc = std::uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
 }
 return crc;
}

// Explicit little-endian format, independent of struct padding and CPU endian:
// magic/version/sequence16/uptime32/button/config/wake/power/flags/IRQ3/CRC16.
inline Slot encode(const Snapshot& value) {
 Slot raw{{kMagic, kVersion, std::uint8_t(value.sequence), std::uint8_t(value.sequence >> 8),
  std::uint8_t(value.uptime_ms), std::uint8_t(value.uptime_ms >> 8),
  std::uint8_t(value.uptime_ms >> 16), std::uint8_t(value.uptime_ms >> 24),
  value.button, value.config, value.wake, value.power, value.flags, value.irq3, 0, 0}};
 const auto crc = crc16(raw.data(), kSlotSize - 2);
 raw[14] = std::uint8_t(crc); raw[15] = std::uint8_t(crc >> 8);
 return raw;
}

// A failed/short read leaves the caller's output untouched. A checksum cannot
// detect every possible multi-bit fault; it rejects incomplete/corrupt records
// unless their bytes happen to constitute another valid record.
inline bool decode(const std::uint8_t* raw, std::size_t length, Snapshot& out) {
 if (!raw || length != kSlotSize || raw[0] != kMagic || raw[1] != kVersion) return false;
 const auto stored_crc = std::uint16_t(raw[14] | (std::uint16_t(raw[15]) << 8));
 if (crc16(raw, kSlotSize - 2) != stored_crc) return false;
 Snapshot value;
 value.sequence = std::uint16_t(raw[2] | (std::uint16_t(raw[3]) << 8));
 value.uptime_ms = std::uint32_t(raw[4]) | (std::uint32_t(raw[5]) << 8)
  | (std::uint32_t(raw[6]) << 16) | (std::uint32_t(raw[7]) << 24);
 value.button = raw[8]; value.config = raw[9]; value.wake = raw[10];
 value.power = raw[11]; value.flags = raw[12]; value.irq3 = raw[13];
 out = value;
 return true;
}
inline bool decode(const Slot& raw, Snapshot& out) { return decode(raw.data(), raw.size(), out); }

// Adjacent alternating writes differ by one, including 65535 -> 0. Equal
// sequence numbers and the ambiguous half-range difference are not newer.
constexpr bool newer(std::uint16_t candidate, std::uint16_t previous) {
 const auto difference = std::uint16_t(candidate - previous);
 return difference != 0 && difference < 0x8000;
}
constexpr std::uint8_t slot_register(std::size_t index) {
 return std::uint8_t(kBaseRegister + index * kSlotSize);
}

struct Selection {
 bool valid = false;
 std::size_t index = 0;
 Snapshot snapshot{};
};
struct PendingWrite {
 std::size_t index = 0;
 Snapshot snapshot{};
 Slot raw{};
};

class Slots {
public:
 // Load the complete bytes observed in one slot, including invalid records.
 // Return value describes read length/index, not CRC validity. Short reads do
 // not replace a previously complete observation; use clear() for a new boot.
 bool load(std::size_t index, const std::uint8_t* raw, std::size_t length) {
  if (index >= kSlotCount || !raw || length != kSlotSize) return false;
  for (std::size_t i = 0; i < kSlotSize; ++i) raw_[index][i] = raw[i];
  observed_[index] = true;
  return true;
 }
 bool load(std::size_t index, const Slot& raw) { return load(index, raw.data(), raw.size()); }
 void clear() { observed_.fill(false); }
 Selection newest() const {
  Selection result;
  for (std::size_t index = 0; index < kSlotCount; ++index) {
   Snapshot value;
   if (observed_[index] && decode(raw_[index], value)
       && (!result.valid || newer(value.sequence, result.snapshot.sequence)))
    result = {true, index, value};
  }
  return result;
 }
 // Preparing never advances state. The caller writes raw to slot_register(index),
 // then reads back all 16 bytes and calls confirm(). On failure, the newest
 // confirmed slot remains intact and the next preparation retries the other.
 PendingWrite prepare(Snapshot value) const {
  const auto latest = newest();
  value.sequence = latest.valid ? std::uint16_t(latest.snapshot.sequence + 1) : 1;
  return {latest.valid ? latest.index ^ 1u : 0u, value, encode(value)};
 }
 bool confirm(const PendingWrite& pending, const std::uint8_t* readback, std::size_t length) {
  if (pending.index >= kSlotCount || !readback || length != kSlotSize) return false;
  Snapshot verified;
  if (!decode(readback, length, verified)) return false;
  for (std::size_t i = 0; i < kSlotSize; ++i)
   if (readback[i] != pending.raw[i]) return false;
  const auto expected = prepare(pending.snapshot);
  // Also reject stale/out-of-order write completions.
  if (pending.index != expected.index || pending.raw != expected.raw) return false;
  return load(pending.index, readback, length);
 }
 bool confirm(const PendingWrite& pending, const Slot& readback) {
  return confirm(pending, readback.data(), readback.size());
 }
private:
 std::array<Slot, kSlotCount> raw_{};
 std::array<bool, kSlotCount> observed_{};
};
} // namespace vibe_pmic_diag
