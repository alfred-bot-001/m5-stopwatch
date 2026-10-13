#include "../main/power_history_logic.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace vibe_power;

struct MemoryFlash {
    std::vector<uint8_t> bytes;
    size_t partial_write = std::numeric_limits<size_t>::max();
    size_t partial_erase = std::numeric_limits<size_t>::max();
    bool read_error = false;
    unsigned writes = 0, erases = 0;
    explicit MemoryFlash(unsigned sectors = 3) : bytes(sectors * kSectorBytes, 0xff) {}
    size_t size() const { return bytes.size(); }
    bool read(size_t offset, void *dst, size_t length) {
        if (read_error || offset + length > bytes.size()) return false;
        std::memcpy(dst, bytes.data() + offset, length); return true;
    }
    bool write(size_t offset, const void *src, size_t length) {
        ++writes;
        assert(offset + length <= bytes.size());
        const auto *p = static_cast<const uint8_t *>(src);
        for (size_t i = 0; i < std::min(length, partial_write); ++i) {
            assert((bytes[offset + i] & p[i]) == p[i]);
            bytes[offset + i] &= p[i];
        }
        const bool ok = partial_write == std::numeric_limits<size_t>::max();
        partial_write = std::numeric_limits<size_t>::max();
        return ok;
    }
    bool erase(size_t offset, size_t length) {
        ++erases;
        assert(offset % kSectorBytes == 0 && length == kSectorBytes);
        assert(offset + length <= bytes.size());
        std::fill_n(bytes.data() + offset, std::min(length, partial_erase), 0xff);
        const bool ok = partial_erase == std::numeric_limits<size_t>::max();
        partial_erase = std::numeric_limits<size_t>::max();
        return ok;
    }
};
static VibePowerSample sample(uint64_t time = 0) {
    VibePowerSample s{};
    s.uptime_ms = time; s.valid = true; s.battery_mv = 3750; s.usb_mv = 5000;
    s.percent = 50; s.charging = 1; s.screen_on = true; s.ptt = false;
    s.radio = true; s.idle = false; s.cpu_mhz = 240;
    s.pmic_mode = 0xa5; s.pmic_out = 0x3b; s.pmic_drive = 0x82; s.pmic_func = 0x9876;
    return s;
}
static Record minute(uint64_t time = 0) { return make_record(Kind::Minute, sample(time), 1); }
static void fill(Journal<MemoryFlash> &journal, unsigned n, unsigned session = 1) {
    for (unsigned i = 0; i < n; ++i)
        assert(journal.append(minute(i * 60000ull), session, 1, 0) == AppendResult::Written);
}
static std::vector<Record> chronological(Journal<MemoryFlash> &journal) {
    std::vector<Record> found;
    if (!journal.count()) return found;
    size_t slot = journal.oldest_slot();
    const size_t end = journal.latest_slot();
    for (size_t i = 0; i < journal.slots(); ++i) {
        Record record; assert(journal.read(slot, record));
        if (record.valid()) found.push_back(record);
        if (slot == end) break;
        slot = (slot + 1) % journal.slots();
    }
    assert(found.size() == journal.count());
    for (size_t i = 1; i < found.size(); ++i)
        assert(found[i - 1].sequence() < found[i].sequence());
    return found;
}
static void records() {
    auto r = minute(1234567890);
    r.identify(0x123456789abcdef0ull, 3, 14, 22);
    assert(r.valid() && r.sequence() == 0x123456789abcdef0ull && r.session() == 3);
    assert(r.uptime_ms() == 1234567890 && r.reset_reason() == 14 && r.dropped() == 22);
    const auto s = r.sample();
    assert(s.valid && s.screen_on && !s.ptt && s.radio && !s.idle);
    assert(s.battery_mv == 3750 && s.usb_mv == 5000 && s.percent == 50 && s.charging == 1);
    assert(s.cpu_mhz == 240 && s.pmic_mode == 0xa5 && s.pmic_out == 0x3b);
    assert(s.pmic_drive == 0x82 && s.pmic_func == 0x9876);
    for (size_t i = 0; i < r.bytes.size(); ++i) {
        auto corrupt = r; corrupt.bytes[i] ^= 0x01; assert(!corrupt.valid());
    }
    auto invalid = sample(); invalid.valid = false;
    auto failed = make_record(Kind::Minute, invalid, 1);
    failed.identify(1, 1, 1, 0);
    const auto f = failed.sample();
    assert(!f.valid && f.percent == -1 && f.charging == -1 && failed.external() == -1);
    assert(f.battery_mv == 0 && f.usb_mv == 0 && f.pmic_mode == 0 && f.pmic_func == 0);
    assert(f.cpu_mhz == 240 && f.screen_on && f.radio);
}
static void ring_wrap_and_reboot() {
    MemoryFlash flash;
    Journal<MemoryFlash> log(flash);
    assert(log.recover() && log.count() == 0 && log.sequence() == 0 && log.slots() == 192);
    for (unsigned i = 1; i <= 2500; ++i) {
        assert(log.append(minute(i * 60000ull), i < 1000 ? 1 : 2, 1, 0) == AppendResult::Written);
        assert(log.sequence() == i);
        assert(log.count() <= 192 && log.count() > std::min(128u, i - 1));
        if (i % 31 == 0) assert(log.recover());
        const auto rows = chronological(log);
        assert(rows.back().sequence() == i);
        assert(rows.back().sequence() - rows.front().sequence() + 1 == rows.size());
    }
    Journal<MemoryFlash> reboot(flash);
    assert(reboot.recover() && reboot.sequence() == 2500 && reboot.max_session() == 2);
    assert(reboot.append(minute(), 3, 7, 0) == AppendResult::Written);
    assert(chronological(reboot).back().session() == 3);
    assert(flash.erases < 40); // Erase once per sector reuse, not per minute.
}
static void torn_writes() {
    for (size_t cut = 0; cut <= kRecordBytes; ++cut) {
        MemoryFlash flash(2);
        Journal<MemoryFlash> log(flash); assert(log.recover()); fill(log, 20);
        flash.partial_write = cut;
        assert(log.append(minute(), 1, 1, 0) == AppendResult::Error && !log.ready());
        // The next reboot either commits the complete CRC record or ignores
        // the partially programmed slot; no existing record is damaged.
        Journal<MemoryFlash> reboot(flash); assert(reboot.recover());
        const unsigned expected = cut == kRecordBytes ? 21 : 20;
        assert(reboot.count() == expected && reboot.sequence() == expected);
        assert(reboot.append(minute(), 2, 1, 0) == AppendResult::Written);
        const auto rows = chronological(reboot);
        assert(rows.size() == expected + 1 && rows.back().sequence() == expected + 1);
    }
    // A torn first record of a recycled sector must not damage the newest
    // intact sector or accidentally reuse sequence numbers from it.
    for (size_t cut = 0; cut < kRecordBytes; ++cut) {
        MemoryFlash flash(2);
        Journal<MemoryFlash> log(flash); assert(log.recover()); fill(log, 128);
        flash.partial_write = cut;
        assert(log.append(minute(), 1, 1, 0) == AppendResult::Error);
        Journal<MemoryFlash> reboot(flash); assert(reboot.recover());
        assert(reboot.sequence() == 128 && reboot.count() == 64);
        assert(reboot.append(minute(), 2, 1, 0) == AppendResult::Written);
        assert(chronological(reboot).back().sequence() == 129);
    }
}
static void torn_erase_and_export_protection() {
    for (size_t cut : {size_t(0), size_t(1), size_t(33), size_t(64), size_t(2048), size_t(4095), size_t(4096)}) {
        MemoryFlash flash(2);
        Journal<MemoryFlash> log(flash); assert(log.recover()); fill(log, 128);
        const auto before = flash.bytes;
        assert(log.append(minute(), 1, 1, 0, false) == AppendResult::Blocked);
        assert(flash.bytes == before && log.sequence() == 128 && log.ready());
        flash.partial_erase = cut;
        assert(log.append(minute(), 1, 1, 0) == AppendResult::Error && !log.ready());
        Journal<MemoryFlash> reboot(flash); assert(reboot.recover());
        assert(reboot.sequence() == 128 && reboot.count() >= 64);
        assert(reboot.append(minute(), 2, 1, 0) == AppendResult::Written);
        const auto rows = chronological(reboot);
        assert(rows.size() == 65 && rows.front().sequence() == 65 && rows.back().sequence() == 129);
    }
}
static void corruption_and_read_failure() {
    MemoryFlash flash;
    Journal<MemoryFlash> log(flash); assert(log.recover()); fill(log, 20);
    flash.bytes[3 * kRecordBytes + 32] ^= 1; // historical record
    flash.bytes[19 * kRecordBytes + 48] ^= 1; // newest record
    assert(log.recover() && log.count() == 18 && log.sequence() == 19);
    assert(log.append(minute(), 2, 1, 0) == AppendResult::Written);
    auto rows = chronological(log);
    assert(rows.size() == 19 && rows.back().sequence() == 20);
    flash.read_error = true;
    assert(!log.recover());
    assert(log.append(minute(), 2, 1, 0) == AppendResult::Error);
    flash.read_error = false;
    assert(log.recover());
}
static void events_and_invalid_readings() {
    Tracker tracker;
    std::vector<Record> events;
    auto emit = [&](const Record &r) { events.push_back(r); };
    auto s = sample();
    for (uint64_t t = 0; t <= 1000; t += 250) { s.uptime_ms = t; tracker.sample(s, emit); }
    assert(events.size() == 2 && events[0].kind() == Kind::Minute);
    assert(events[1].kind() == Kind::InitialState); // Already charging at boot.
    s.usb_mv = 0; s.charging = 0;
    for (uint64_t t = 1250; t <= 1750; t += 250) { s.uptime_ms = t; tracker.sample(s, emit); }
    s.usb_mv = 5000; s.charging = 1; s.uptime_ms = 2000; tracker.sample(s, emit);
    assert(events.size() == 2); // Short bounce cannot invent a stop/start.
    s.valid = false;
    for (uint64_t t = 2250; t <= 60000; t += 250) { s.uptime_ms = t; tracker.sample(s, emit); }
    assert(events.size() == 3 && events.back().kind() == Kind::Minute);
    assert(!events.back().sample().valid && events.back().sample().charging == -1);
    s.valid = true; s.usb_mv = 0; s.charging = 0;
    for (uint64_t t = 60250; t <= 61000; t += 250) { s.uptime_ms = t; tracker.sample(s, emit); }
    assert(events.size() == 3); // Recovery must debounce a whole second.
    s.uptime_ms = 61250; tracker.sample(s, emit);
    assert(events.size() == 5 && events[3].kind() == Kind::ExternalOff && events[4].kind() == Kind::ChargeStop);
    s.usb_mv = 3800; s.charging = -1;
    for (uint64_t t = 61500; t <= 70000; t += 250) { s.uptime_ms = t; tracker.sample(s, emit); }
    assert(events.size() == 5); // Ambiguous input voltage/state cannot toggle events.
    s.usb_mv = 5000; s.charging = 1;
    for (uint64_t t = 70250; t <= 71250; t += 250) { s.uptime_ms = t; tracker.sample(s, emit); }
    assert(events.size() == 7 && events[5].kind() == Kind::ExternalOn && events[6].kind() == Kind::ChargeStart);
    // Long valid-poll gaps do not count as continuous evidence.
    s.usb_mv = 0; s.charging = 0; s.uptime_ms = 80000; tracker.sample(s, emit);
    s.uptime_ms = 90000; tracker.sample(s, emit); assert(events.size() == 7);
    s.uptime_ms = 91000; tracker.sample(s, emit); assert(events.size() == 9);
    // Uptime beyond the 32-bit millisecond wrap still emits one minute record.
    Tracker long_running;
    events.clear(); s = sample(uint64_t(UINT32_MAX) + 60000); long_running.sample(s, emit);
    assert(events.size() == 1 && events[0].uptime_ms() > UINT32_MAX);
    s.uptime_ms += 60000; long_running.sample(s, emit); assert(events.size() == 2);
}
int main() {
    records(); ring_wrap_and_reboot(); torn_writes();
    torn_erase_and_export_protection(); corruption_and_read_failure(); events_and_invalid_readings();
    std::puts("power history: encoding/CRC, ring reuse, reboot/session, torn writes/erases, export protection, I/O faults, debounced charge/power events and unknown readings passed");
}
