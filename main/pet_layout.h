#pragma once

// Shared with the C USB snapshot header. Use the whole physical round panel;
// no inset software fence is drawn.
#define PET_SCREEN_SIZE 466
#define PET_SCREEN_CENTER 233.0f
#define PET_SCREEN_RADIUS 231.0f
#define PET_SPRITE_SIZE 240
// The original alpha silhouette fits radius 115.4 at this sprite size; 124
// also covers the largest impact squash and sampling/antialiasing margins.
#define PET_BODY_RADIUS 124.0f
