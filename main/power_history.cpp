#include "power_history.hpp"
#include "power_history_logic.hpp"
#include <atomic>
#include <cstdio>
#include <inttypes.h>
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
using namespace vibe_power;
constexpr size_t kQueueLength = 64;
constexpr int64_t kExportTimeoutUs = 15000000;
class PartitionStorage {
public:
    const esp_partition_t *partition = nullptr;
    bool recovering = true;
    unsigned scan_reads = 0;
    size_t size() const { return partition ? partition->size : 0; }
    bool read(size_t offset, void *dst, size_t length) {
        const bool ok = esp_partition_read(partition, offset, dst, length) == ESP_OK;
        if (recovering && ++scan_reads % kRecordsPerSector == 0)
            vTaskDelay(pdMS_TO_TICKS(1));
        return ok;
    }
    bool write(size_t offset, const void *src, size_t length) {
        return esp_partition_write(partition, offset, src, length) == ESP_OK;
    }
    bool erase(size_t offset, size_t length) {
        return esp_partition_erase_range(partition, offset, length) == ESP_OK;
    }
};
PartitionStorage storage;
Journal<PartitionStorage> journal(storage);
Tracker tracker;
QueueHandle_t queue = nullptr;
SemaphoreHandle_t mutex = nullptr;
std::atomic<bool> started{false}, ready{false}, fast_ptt{false}, sampled_ptt{false}, pending{false};
std::atomic<bool> exporting{false}, export_expired{false};
std::atomic<uint32_t> dropped{0}, errors{0}, session{0}, records{0}, capacity{0};
portMUX_TYPE sequence_lock = portMUX_INITIALIZER_UNLOCKED;
uint64_t sequence = 0;
uint32_t reset_reason = 0;
// Protected by mutex. The queue and ordinary status fields use their own
// synchronization so status()/sample()/set_ptt() never wait for flash.
struct Export {
    unsigned phase = 0;
    size_t slot = 0, slots_left = 0, expected_records = 0, emitted_records = 0;
    uint64_t sequence = 0;
    int64_t touched_us = 0;
} snapshot;
struct Unlock {
    ~Unlock() { xSemaphoreGive(mutex); }
};
void publish() {
    records = journal.count(); capacity = journal.slots();
    portENTER_CRITICAL(&sequence_lock); sequence = journal.sequence(); portEXIT_CRITICAL(&sequence_lock);
    ready = journal.ready();
}
void expire_export() {
    if (!exporting.load() || xSemaphoreTake(mutex, 0) != pdTRUE) return;
    Unlock unlock;
    if (exporting.load() && esp_timer_get_time() - snapshot.touched_us > kExportTimeoutUs) {
        exporting = false; export_expired = true;
    }
}
void writer(void *) {
    // Recovery belongs to this low-priority task, not app_main or the USB/HID
    // service. It reads 1 MiB in small units and yields once per flash sector.
    // Journal recovery uses the storage reader below; the explicit scan itself
    // is bounded (16,384 records) and normally completes within a second.
    if (!journal.recover()) { ++errors; vTaskDelete(nullptr); return; }
    const uint32_t previous_session = journal.max_session();
    if (previous_session == UINT32_MAX) { ++errors; vTaskDelete(nullptr); return; }
    storage.recovering = false;
    session = previous_session + 1;
    publish();
    VibePowerSample boot{};
    boot.percent = -1; boot.charging = -1;
    boot.cpu_mhz = esp_clk_cpu_freq() / 1000000;
    Record record = make_record(Kind::Boot, boot, -1);
    bool have_record = true;
    pending = true;
    for (;;) {
        expire_export();
        if (!have_record) {
            if (xQueueReceive(queue, &record, pdMS_TO_TICKS(100)) != pdTRUE) continue;
            have_record = true; pending = true;
        }
        // No automatic light/deep sleep or I2C work is performed here. Flash
        // erases/writes can briefly stall caches, so defer whenever speech or
        // a host snapshot is active. An already-started erase is not cancellable.
        if (fast_ptt.load() || sampled_ptt.load() || exporting.load()) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) continue;
        AppendResult result = AppendResult::Blocked;
        if (!fast_ptt.load() && !sampled_ptt.load() && !exporting.load())
            result = journal.append(record, session.load(), reset_reason, dropped.load());
        publish();
        xSemaphoreGive(mutex);
        if (result == AppendResult::Written) { have_record = false; pending = false; }
        else if (result == AppendResult::Error) {
            ++errors; ready = false;
            // Do not keep programming a flash region after an I/O error. The
            // next boot rescans CRC records and recovers past any torn write.
            pending = false; vTaskDelete(nullptr); return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
size_t error_line(char *out, size_t length, const char *message) {
    const int count = snprintf(out, length, "# error=%s\n", message);
    return count > 0 && size_t(count) < length ? size_t(count) : 0;
}
} // namespace

extern "C" bool vibe_power_history_begin(uint32_t reason) {
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) return queue != nullptr;
    storage.partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                esp_partition_subtype_t(0x40), "powlog");
    if (!storage.partition || storage.partition->address != 0x430000
        || storage.partition->size != 1024 * 1024) { ++errors; return false; }
    reset_reason = reason;
    mutex = xSemaphoreCreateMutex();
    queue = xQueueCreate(kQueueLength, sizeof(Record));
    if (!mutex || !queue) {
        if (queue) vQueueDelete(queue);
        if (mutex) vSemaphoreDelete(mutex);
        queue = nullptr; mutex = nullptr; ++errors; return false;
    }
    if (xTaskCreate(writer, "power_history", 4096, nullptr, 1, nullptr) != pdPASS) {
        vQueueDelete(queue); vSemaphoreDelete(mutex);
        queue = nullptr; mutex = nullptr; ++errors; return false;
    }
    return true;
}
extern "C" void vibe_power_history_set_ptt(bool active) { fast_ptt = active; }
extern "C" void vibe_power_history_sample(const VibePowerSample *sample) {
    if (!sample) return;
    sampled_ptt = sample->ptt;
    if (!queue) return;
    tracker.sample(*sample, [](const Record &record) {
        if (xQueueSend(queue, &record, 0) != pdTRUE) ++dropped;
    });
}
extern "C" void vibe_power_history_status(VibePowerHistoryStatus *out) {
    if (!out) return;
    *out = {};
    out->ready = ready.load(); out->exporting = exporting.load();
    out->session = session.load(); out->records = records.load();
    out->capacity = capacity.load();
    // ready is published only by a successfully created worker, after queue
    // allocation is final. An initialization failure may still delete queue;
    // the already-running USB status task must not inspect it in that phase.
    out->queued = out->ready ? uxQueueMessagesWaiting(queue) + unsigned(pending.load()) : 0;
    out->dropped = dropped.load(); out->errors = errors.load();
    portENTER_CRITICAL(&sequence_lock); out->newest_sequence = sequence; portEXIT_CRITICAL(&sequence_lock);
}
extern "C" bool vibe_power_history_export_begin(void) {
    if (!ready.load() || !mutex || xSemaphoreTake(mutex, 0) != pdTRUE) return false;
    Unlock unlock;
    if (!ready.load() || exporting.load()) return false;
    snapshot = {};
    snapshot.slot = journal.oldest_slot();
    snapshot.expected_records = journal.count();
    snapshot.sequence = journal.sequence();
    if (snapshot.expected_records)
        snapshot.slots_left = (journal.latest_slot() + journal.slots() - snapshot.slot)
                              % journal.slots() + 1;
    snapshot.touched_us = esp_timer_get_time();
    export_expired = false; exporting = true;
    return true;
}
extern "C" size_t vibe_power_history_export_next(char *out, size_t length) {
    if (!out || length < 384 || !mutex) return 0;
    if (xSemaphoreTake(mutex, 0) != pdTRUE) return SIZE_MAX;
    Unlock unlock;
    if (export_expired.exchange(false)) return error_line(out, length, "snapshot_timeout");
    if (!exporting.load()) return 0;
    if (esp_timer_get_time() - snapshot.touched_us > kExportTimeoutUs) {
        exporting = false;
        return error_line(out, length, "snapshot_timeout");
    }
    snapshot.touched_us = esp_timer_get_time();
    if (snapshot.phase++ == 0) {
        return size_t(snprintf(out, length,
            "# format=power_history_1 snapshot_seq=%" PRIu64 " records=%u capacity=%u dropped=%u errors=%u time=session_uptime_ms percent=voltage_estimate\n",
            snapshot.sequence, unsigned(snapshot.expected_records), unsigned(journal.slots()),
            unsigned(dropped.load()), unsigned(errors.load())));
    }
    if (snapshot.phase == 2) {
        return size_t(snprintf(out, length,
            "seq,session,uptime_ms,event,valid,battery_mv,usb_mv,estimated_percent,charging,external_power,screen_on,ptt,radio,idle,cpu_mhz,pmic_mode,pmic_out,pmic_drive,pmic_func,reset_reason,dropped\n"));
    }
    if (!snapshot.slots_left) {
        exporting = false;
        if (snapshot.emitted_records != snapshot.expected_records)
            return error_line(out, length, "snapshot_record_count_mismatch");
        return 0;
    }
    // Exactly one 64-byte read per call. Torn slots yield to the next USB
    // iteration rather than scanning a large hole in the control task.
    Record record;
    const size_t slot = snapshot.slot;
    snapshot.slot = (snapshot.slot + 1) % journal.slots(); --snapshot.slots_left;
    if (!journal.read(slot, record)) {
        ++errors; exporting = false;
        return error_line(out, length, "flash_read_failed");
    }
    if (!record.valid()) return SIZE_MAX;
    if (record.sequence() > snapshot.sequence) {
        ++errors; exporting = false;
        return error_line(out, length, "snapshot_changed");
    }
    ++snapshot.emitted_records;
    const VibePowerSample s = record.sample();
    return size_t(snprintf(out, length,
        "%" PRIu64 ",%" PRIu32 ",%" PRIu64 ",%s,%u,%u,%u,%d,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%" PRIu32 ",%" PRIu32 "\n",
        record.sequence(), record.session(), s.uptime_ms, kind_name(record.kind()), unsigned(s.valid),
        unsigned(s.battery_mv), unsigned(s.usb_mv), int(s.percent), int(s.charging), record.external(),
        unsigned(s.screen_on), unsigned(s.ptt), unsigned(s.radio), unsigned(s.idle), unsigned(s.cpu_mhz),
        unsigned(s.pmic_mode), unsigned(s.pmic_out), unsigned(s.pmic_drive), unsigned(s.pmic_func),
        record.reset_reason(), record.dropped()));
}
extern "C" void vibe_power_history_export_end(void) {
    // Clearing the atomic is enough: snapshot fields are replaced only by a
    // later begin() under mutex. Never make the USB disconnect path wait.
    exporting = false; export_expired = false;
}
