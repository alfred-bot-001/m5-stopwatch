#pragma once

// Shared with the C USB snapshot header. Use the whole physical round panel;
// no inset software fence is drawn.
#define PET_SCREEN_SIZE 466
#define PET_SCREEN_CENTER 233.0f
#define PET_SCREEN_RADIUS 231.0f
#define PET_SPRITE_SIZE 240
// All eight aligned expression silhouettes fit radius 120.74 including the
// largest impact squash; 124 includes sampling/antialiasing margins and Zs.
#define PET_BODY_RADIUS 124.0f
