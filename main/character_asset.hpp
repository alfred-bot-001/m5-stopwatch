#pragma once
#include <cstddef>
#include <cstdint>
#include "pet_layout.h"

#define FROG_ASSET_SYMBOL(name) \
 extern const uint8_t frog_##name##_png_start[] asm("_binary_frog_" #name "_png_start"); \
 extern const uint8_t frog_##name##_png_end[] asm("_binary_frog_" #name "_png_end")
FROG_ASSET_SYMBOL(happy);
FROG_ASSET_SYMBOL(laugh);
FROG_ASSET_SYMBOL(surprised);
FROG_ASSET_SYMBOL(shy);
FROG_ASSET_SYMBOL(sad);
FROG_ASSET_SYMBOL(angry);
FROG_ASSET_SYMBOL(sleepy);
FROG_ASSET_SYMBOL(wink);
#undef FROG_ASSET_SYMBOL

namespace character_asset {
enum Index : uint8_t { Happy,Laugh,Surprised,Shy,Sad,Angry,Sleepy,Wink,Count };
struct Asset {const uint8_t *data;std::size_t size;int source_top;};
#define FROG_ASSET(name,top) {frog_##name##_png_start,std::size_t(frog_##name##_png_end-frog_##name##_png_start),top}
inline const Asset assets[]={FROG_ASSET(happy,0),FROG_ASSET(laugh,0),FROG_ASSET(surprised,0),FROG_ASSET(shy,0),
 FROG_ASSET(sad,0),FROG_ASSET(angry,0),FROG_ASSET(sleepy,74),FROG_ASSET(wink,0)};
#undef FROG_ASSET
inline constexpr int source_width=477,source_height=385;
inline constexpr int width=PET_SPRITE_SIZE,height=PET_SPRITE_SIZE;
inline constexpr float scale=218.f/source_width;
inline constexpr int body_x=11,body_y=44,body_width=218,body_height=176;
// All eight files share this body rectangle. Sleepy's extra 74 source rows hold
// its Z decoration; display composition relocates that decoration within bounds.
inline constexpr float left_eye_x=body_x+128.f*scale,left_eye_y=body_y+78.f*scale;
inline constexpr float right_eye_x=body_x+348.f*scale,right_eye_y=left_eye_y;
// The source pupils slightly overlap their original white circles. A ~1.7 px
// larger white replacement covers those dark edges before moving the pupils.
inline constexpr float white_radius=44.5f*scale,pupil_radius=30.5f*scale;
inline constexpr float max_pupil_x=1.5f,max_pupil_y=1.0f;
inline constexpr float mouth_x=body_x+238.f*scale,mouth_y=body_y+246.f*scale;
}
