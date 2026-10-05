#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void vibe_usb_init(void);
void vibe_connected(bool connected);
void vibe_service(void);
bool vibe_report(uint8_t report[8]);
bool vibe_wheel(int8_t *wheel);
void vibe_pcm(int16_t *data, size_t samples);
void vibe_status(char *buffer, size_t size);
void vibe_request_power_reset(void);
void vibe_note_boot_intent(void);
uint32_t vibe_usb_lifecycle(void);
void vibe_usb_audio_status(char *buffer, size_t size);
uint8_t *vibe_snapshot(size_t *size);
#ifdef __cplusplus
}
#endif
