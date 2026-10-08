#pragma once
#include <cstdint>

// M5PM1 BTN_CFG_1 (0x49), register map HW5/SW6 or S:
// LONG[4:3]=3 means four seconds; SINGLE_RESET_DIS[0]=1 ignores
// accidental single-click resets. Preserve DL_LOCK[7], DBL and SINGLE.
// Never set DL_LOCK to prevent accidents: it also blocks recovery downloads.
constexpr uint8_t guarded_power_key_config(uint8_t existing) {
 return uint8_t(existing | 0x18u | 0x01u);
}
