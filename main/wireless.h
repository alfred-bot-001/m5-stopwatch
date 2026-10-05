#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// Start once in a background task; return task-launch error without rebooting.
// The eventual initialization result/stage is available through view/status.
esp_err_t wireless_init(void);
bool wireless_ready(void);
void wireless_state(bool listening,uint32_t session,uint16_t battery_mv,float level);
void wireless_keyboard(const uint8_t keys[8]);
void wireless_wheel(int8_t wheel);
void wireless_audio(const int16_t *pcm,size_t samples);
typedef struct {
 bool connected,host,listening,initialized;
 uint16_t battery_mv,level;uint32_t received,lost;
 esp_err_t error;const char *stage;
} wireless_view_t;
void wireless_view(wireless_view_t *view);
void wireless_status(char *out,size_t size);
// Receiver functions; sender implementations are harmless no-ops.
void wireless_usb(bool connected);
bool wireless_report(uint8_t keys[8]);
bool wireless_read_wheel(int8_t *wheel);
void wireless_pcm(int16_t *out,size_t samples);
#ifdef __cplusplus
}
#endif
