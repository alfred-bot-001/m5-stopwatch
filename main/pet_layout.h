#pragma once

// Shared with the C USB snapshot header. Use the whole physical round panel;
// no inset software fence is drawn.
#define PET_SCREEN_SIZE 466
#define PET_SCREEN_CENTER 233.0f
#define PET_SCREEN_RADIUS 231.0f
#define PET_SPRITE_SIZE 240
// The shared elastic transform limits every expression silhouette and Z to
// radius 120; 124 includes sampling margins for the conservative collision body.
#define PET_BODY_RADIUS 124.0f
