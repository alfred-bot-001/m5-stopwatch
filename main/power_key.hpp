#pragma once
#include <cstdint>

// M5PM1 BTN_CFG_1 (0x49), register map HW5/SW6 or S:
// LONG[4:3]=3 means four seconds; SINGLE_RESET_DIS[0]=1 ignores
// accidental single-click resets. Preserve DBL and SINGLE timing fields.
// DL_LOCK[7] blocks the PMIC hardware download path. Setting it is an explicit
// opt-in: clearing the build option cannot unlock an already locked PMIC;
// unlocking requires a complete PMIC power cycle (including battery power).
// The default preserves the existing lock state and hardware recovery path.
constexpr uint8_t guarded_power_key_config(uint8_t existing,
                                          bool lock_download=false) {
 return uint8_t(existing | 0x18u | 0x01u | (lock_download ? 0x80u : 0u));
}
