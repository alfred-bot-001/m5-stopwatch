#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Power readings are supplied by the main task's existing PMIC poll. No wall
// clock is assumed: session + uptime_ms identify when a reading was taken.
typedef struct {
    uint64_t uptime_ms;
    uint16_t battery_mv;
    uint16_t usb_mv;
    int8_t percent;              // -1 when unknown; voltage estimate, not capacity.
    int8_t charging;             // -1 unknown, 0 inactive, 1 active.
    uint16_t cpu_mhz;
    uint8_t pmic_mode;
    uint8_t pmic_out;
    uint8_t pmic_drive;
    uint16_t pmic_func;
    bool valid;
    bool screen_on;
    bool ptt;
    bool radio;
    bool idle;
} VibePowerSample;

typedef struct {
    bool ready;
    bool exporting;
    uint32_t session;
    uint32_t records;
    uint32_t capacity;
    uint32_t queued;
    uint32_t dropped;
    uint32_t errors;
    uint64_t newest_sequence;
} VibePowerHistoryStatus;

// true means the background task started; status.ready becomes true after its
// recovery scan. Sampling is nonblocking and does not perform I2C or flash I/O.
bool vibe_power_history_begin(uint32_t reset_reason);
void vibe_power_history_sample(const VibePowerSample *sample);
// Call from the fast button service so the writer need not wait for a PMIC poll.
void vibe_power_history_set_ptt(bool active);
void vibe_power_history_status(VibePowerHistoryStatus *status);

// One read-only CSV snapshot at a time. begin returns false while recovering,
// unavailable, or already exporting. next needs >= 384 bytes; it returns one
// header/data/comment line, SIZE_MAX to retry later without sending, or zero
// at EOF. Errors are emitted as # error=... comments before EOF. Call end on USB disconnect or
// cancellation. A stalled export expires after 15 seconds without next().
// A snapshot contains only records present at begin(); writes remain queued
// during export, so neither reads nor the USB button service waits on an erase.
bool vibe_power_history_export_begin(void);
size_t vibe_power_history_export_next(char *out, size_t capacity);
void vibe_power_history_export_end(void);

#ifdef __cplusplus
}
#endif
