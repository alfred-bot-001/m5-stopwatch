#pragma once
#include <cstddef>
#include <cstdint>
#include "pet_layout.h"

extern const uint8_t orange_character_png_start[] asm("_binary_orange_character_png_start");
extern const uint8_t orange_character_png_end[] asm("_binary_orange_character_png_end");

namespace character_asset {
inline const uint8_t *const png=orange_character_png_start;
inline const std::size_t png_size=orange_character_png_end-orange_character_png_start;
inline constexpr int source_width=1254,source_height=1254;
inline constexpr int width=PET_SPRITE_SIZE,height=PET_SPRITE_SIZE;
// Default centre is the middle of the play area; motion overrides this origin.
inline constexpr int x=(PET_SCREEN_SIZE-PET_SPRITE_SIZE)/2,y=x;
// Measured white-eye centres in the source, scaled to the cached sprite.
inline constexpr float left_eye_x=71.4123f,left_eye_y=74.0433f;
inline constexpr float right_eye_x=168.7683f,right_eye_y=74.0675f;
inline constexpr float pupil_radius=13.5f,max_pupil_x=2.25f,max_pupil_y=1.5f;
}
