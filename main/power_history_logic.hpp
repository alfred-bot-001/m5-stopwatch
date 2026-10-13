#pragma once
#include "power_history.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace vibe_power {
constexpr size_t kRecordBytes = 64;
constexpr size_t kSectorBytes = 4096;
constexpr size_t kRecordsPerSector = kSectorBytes / kRecordBytes;
constexpr size_t kMaxSectors = 256;
constexpr uint32_t kMagic = 0x31525750; // PWR1, little endian.
enum class Kind : uint8_t {
    Boot = 1, Minute, InitialState, ExternalOn, ExternalOff,
    ChargeStart, ChargeStop,
};
inline const char *kind_name(Kind kind) {
    switch (kind) {
    case Kind::Boot: return "boot";
    case Kind::Minute: return "minute";
    case Kind::InitialState: return "initial_state";
    case Kind::ExternalOn: return "external_power_on";
    case Kind::ExternalOff: return "external_power_off";
    case Kind::ChargeStart: return "charge_start";
    case Kind::ChargeStop: return "charge_stop";
    }
    return "invalid";
}
inline uint32_t crc32(const uint8_t *bytes, size_t length) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
inline void put_le(uint8_t *p, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) p[i] = uint8_t(value >> (i * 8));
}
inline uint64_t get_le(const uint8_t *p, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= uint64_t(p[i]) << (i * 8);
    return value;
}
struct Record {
    std::array<uint8_t, kRecordBytes> bytes{};
    uint64_t sequence() const { return get_le(bytes.data() + 8, 8); }
    uint32_t session() const { return uint32_t(get_le(bytes.data() + 16, 4)); }
    uint64_t uptime_ms() const { return get_le(bytes.data() + 24, 8); }
    Kind kind() const { return Kind(bytes[5]); }
    uint16_t flags() const { return uint16_t(get_le(bytes.data() + 6, 2)); }
    uint32_t reset_reason() const { return uint32_t(get_le(bytes.data() + 20, 4)); }
    uint32_t dropped() const { return uint32_t(get_le(bytes.data() + 48, 4)); }
    int external() const { return int(int8_t(bytes[46])); }
    VibePowerSample sample() const {
        VibePowerSample s{};
        s.uptime_ms = uptime_ms();
        s.battery_mv = uint16_t(get_le(bytes.data() + 32, 2));
        s.usb_mv = uint16_t(get_le(bytes.data() + 34, 2));
        s.percent = int8_t(bytes[36]);
        s.charging = int8_t(bytes[37]);
        s.cpu_mhz = uint16_t(get_le(bytes.data() + 38, 2));
        s.pmic_mode = bytes[40]; s.pmic_out = bytes[41];
        s.pmic_drive = bytes[42];
        s.pmic_func = uint16_t(get_le(bytes.data() + 44, 2));
        s.valid = flags() & 1; s.screen_on = flags() & 2;
        s.ptt = flags() & 4; s.radio = flags() & 8; s.idle = flags() & 16;
        return s;
    }
    bool valid() const {
        return get_le(bytes.data(), 4) == kMagic && bytes[4] == 1
            && bytes[5] >= uint8_t(Kind::Boot) && bytes[5] <= uint8_t(Kind::ChargeStop)
            && sequence() != 0
            && get_le(bytes.data() + 60, 4) == crc32(bytes.data(), 60);
    }
    bool erased() const {
        for (uint8_t value : bytes) if (value != 0xff) return false;
        return true;
    }
    void identify(uint64_t sequence, uint32_t session, uint32_t reset_reason,
                  uint32_t dropped) {
        put_le(bytes.data() + 8, sequence, 8);
        put_le(bytes.data() + 16, session, 4);
        put_le(bytes.data() + 20, reset_reason, 4);
        put_le(bytes.data() + 48, dropped, 4);
        put_le(bytes.data() + 60, crc32(bytes.data(), 60), 4);
    }
};
inline Record make_record(Kind kind, const VibePowerSample &sample, int external) {
    Record record;
    auto *p = record.bytes.data();
    put_le(p, kMagic, 4); p[4] = 1; p[5] = uint8_t(kind);
    const unsigned flags = unsigned(sample.valid) | (unsigned(sample.screen_on) << 1)
        | (unsigned(sample.ptt) << 2) | (unsigned(sample.radio) << 3)
        | (unsigned(sample.idle) << 4);
    put_le(p + 6, flags, 2); put_le(p + 24, sample.uptime_ms, 8);
    // Invalid readings remain explicitly unknown instead of copying stale PMIC
    // values into the history. CPU/display/radio fields are independent of I2C.
    put_le(p + 32, sample.valid ? sample.battery_mv : 0, 2);
    put_le(p + 34, sample.valid ? sample.usb_mv : 0, 2);
    p[36] = uint8_t(sample.valid ? sample.percent : -1);
    p[37] = uint8_t(sample.valid ? sample.charging : -1);
    put_le(p + 38, sample.cpu_mhz, 2);
    if (sample.valid) {
        p[40] = sample.pmic_mode; p[41] = sample.pmic_out;
        p[42] = sample.pmic_drive; put_le(p + 44, sample.pmic_func, 2);
    }
    p[46] = uint8_t(sample.valid ? external : -1);
    return record;
}

// A failed/gapped read discards the pending candidate, never the committed
// state. Resuming requires one whole second of fresh, contiguous valid polls.
class DebouncedState {
public:
    enum class Change { None, Initial, Transition };
    Change sample(uint64_t now, int value) {
        if (value < 0 || value > 1) { candidate_ = -1; return Change::None; }
        if (candidate_ != value || now < last_sample_ || now - last_sample_ > 1000) {
            candidate_ = value; since_ = now;
        }
        last_sample_ = now;
        if (now - since_ < 1000 || state_ == value) return Change::None;
        const bool initial = state_ < 0;
        state_ = value;
        return initial ? Change::Initial : Change::Transition;
    }
    int state() const { return state_; }
private:
    int state_ = -1, candidate_ = -1;
    uint64_t since_ = 0, last_sample_ = 0;
};
class Tracker {
public:
    template<class Emit> void sample(const VibePowerSample &s, Emit emit) {
        int external = -1;
        if (s.valid) {
            if (s.usb_mv >= 4000) external = 1;
            else if (s.usb_mv <= 3500) external = 0;
        }
        const auto power_change = external_.sample(s.uptime_ms, external);
        const auto charge_change = charging_.sample(s.uptime_ms,
                                                    s.valid ? s.charging : -1);
        const uint64_t minute = s.uptime_ms / 60000;
        if (minute != minute_) {
            minute_ = minute;
            emit(make_record(Kind::Minute, s, external));
        }
        if (power_change == DebouncedState::Change::Initial
            || charge_change == DebouncedState::Change::Initial)
            emit(make_record(Kind::InitialState, s, external));
        if (power_change == DebouncedState::Change::Transition)
            emit(make_record(external_.state() ? Kind::ExternalOn : Kind::ExternalOff,
                             s, external));
        if (charge_change == DebouncedState::Change::Transition)
            emit(make_record(charging_.state() ? Kind::ChargeStart : Kind::ChargeStop,
                             s, external));
    }
private:
    uint64_t minute_ = std::numeric_limits<uint64_t>::max();
    DebouncedState external_, charging_;
};

enum class AppendResult { Written, Blocked, Error };
// Storage must implement size(), read(offset, dst, length), write(offset, src,
// length), erase(offset, length). Only this class chooses erase boundaries.
// A record is committed only when all bytes and its CRC have reached flash.
// A torn record is skipped after reboot; existing committed records survive.
template<class Storage> class Journal {
public:
    explicit Journal(Storage &storage) : storage_(storage) {}
    bool recover() {
        ready_ = false; counts_.fill(0); count_ = 0; max_sequence_ = 0;
        max_session_ = 0; next_slot_ = 0; oldest_slot_ = 0; latest_slot_ = 0;
        if (storage_.size() < kSectorBytes || storage_.size() % kSectorBytes
            || storage_.size() / kSectorBytes > kMaxSectors) return false;
        slots_ = storage_.size() / kRecordBytes;
        uint64_t oldest_sequence = std::numeric_limits<uint64_t>::max();
        for (size_t slot = 0; slot < slots_; ++slot) {
            Record record;
            if (!read(slot, record)) return false;
            if (!record.valid()) continue;
            ++counts_[slot / kRecordsPerSector]; ++count_;
            if (record.sequence() < oldest_sequence) {
                oldest_sequence = record.sequence(); oldest_slot_ = slot;
            }
            if (record.sequence() > max_sequence_) {
                max_sequence_ = record.sequence(); latest_slot_ = slot;
            }
            if (record.session() > max_session_) max_session_ = record.session();
        }
        if (count_) next_slot_ = (latest_slot_ + 1) % slots_;
        ready_ = true;
        return true;
    }
    AppendResult append(Record record, uint32_t session, uint32_t reset_reason,
                        uint32_t dropped, bool allow_erase = true) {
        if (!ready_ || max_sequence_ == std::numeric_limits<uint64_t>::max())
            return AppendResult::Error;
        for (size_t tried = 0; tried < slots_; ++tried) {
            const size_t sector = next_slot_ / kRecordsPerSector;
            Record existing;
            if (!read(next_slot_, existing)) return fail();
            if (next_slot_ % kRecordsPerSector == 0) {
                // Even if the first slot is erased, the rest can contain a torn
                // erase or older records. Verify the entire sector before use.
                bool all_erased = existing.erased();
                for (size_t i = 1; all_erased && i < kRecordsPerSector; ++i) {
                    if (!read(next_slot_ + i, existing)) return fail();
                    all_erased = existing.erased();
                }
                if (!all_erased) {
                    if (!allow_erase) return AppendResult::Blocked;
                    if (!storage_.erase(sector * kSectorBytes, kSectorBytes)) return fail();
                    count_ -= counts_[sector]; counts_[sector] = 0;
                    if (count_ && oldest_slot_ / kRecordsPerSector == sector) {
                        if (!find_oldest_after_sector(sector)) return fail();
                    }
                }
            } else if (!existing.erased()) {
                // Power could fail halfway through this slot. Do not program
                // over it: skip to a blank slot or the next reclaimable sector.
                next_slot_ = (next_slot_ + 1) % slots_;
                continue;
            }
            record.identify(max_sequence_ + 1, session, reset_reason, dropped);
            if (!storage_.write(next_slot_ * kRecordBytes, record.bytes.data(), kRecordBytes))
                return fail();
            Record check;
            if (!read(next_slot_, check) || check.bytes != record.bytes || !check.valid())
                return fail();
            if (!count_) oldest_slot_ = next_slot_;
            ++count_; ++counts_[sector]; ++max_sequence_;
            if (session > max_session_) max_session_ = session;
            latest_slot_ = next_slot_; next_slot_ = (next_slot_ + 1) % slots_;
            return AppendResult::Written;
        }
        return fail();
    }
    bool read(size_t slot, Record &record) {
        return slot < slots_ && storage_.read(slot * kRecordBytes, record.bytes.data(), kRecordBytes);
    }
    bool ready() const { return ready_; }
    uint64_t sequence() const { return max_sequence_; }
    uint32_t max_session() const { return max_session_; }
    size_t count() const { return count_; }
    size_t slots() const { return slots_; }
    size_t oldest_slot() const { return oldest_slot_; }
    size_t latest_slot() const { return latest_slot_; }
private:
    AppendResult fail() { ready_ = false; return AppendResult::Error; }
    bool find_oldest_after_sector(size_t erased_sector) {
        const size_t sectors = slots_ / kRecordsPerSector;
        for (size_t step = 1; step < sectors; ++step) {
            const size_t sector = (erased_sector + step) % sectors;
            if (!counts_[sector]) continue;
            for (size_t i = 0; i < kRecordsPerSector; ++i) {
                const size_t slot = sector * kRecordsPerSector + i;
                Record record;
                if (!read(slot, record)) return false;
                if (record.valid()) { oldest_slot_ = slot; return true; }
            }
            return false;
        }
        return false;
    }
    Storage &storage_;
    std::array<uint16_t, kMaxSectors> counts_{};
    size_t slots_ = 0, count_ = 0, next_slot_ = 0, oldest_slot_ = 0, latest_slot_ = 0;
    uint64_t max_sequence_ = 0;
    uint32_t max_session_ = 0;
    bool ready_ = false;
};
} // namespace vibe_power
