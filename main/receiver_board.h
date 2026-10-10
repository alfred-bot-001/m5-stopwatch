#pragma once
// Keep each receiver's USB identity distinct even when both are attached.
#ifdef VIBE_RECEIVER_GEEK
#define VIBE_RX_USB_PID 0x4022
#define VIBE_RX_DEVICE_ID "D4059278988C"
#define VIBE_RX_USB_PRODUCT "StopWatch Receiver GEEK"
#define VIBE_RX_USB_AUDIO "StopWatch GEEK Microphone"
#define VIBE_RX_WIDTH 240
#define VIBE_RX_HEIGHT 135
#else
#define VIBE_RX_USB_PID 0x4021
#define VIBE_RX_DEVICE_ID "9C139E8AC480"
#define VIBE_RX_USB_PRODUCT "StopWatch Receiver C480"
#define VIBE_RX_USB_AUDIO "StopWatch Wireless Microphone"
#define VIBE_RX_WIDTH 320
#define VIBE_RX_HEIGHT 240
#endif
