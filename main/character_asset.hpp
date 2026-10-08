#pragma once
#include <cstddef>
#include <cstdint>

extern const uint8_t orange_character_png_start[] asm("_binary_orange_character_png_start");
extern const uint8_t orange_character_png_end[] asm("_binary_orange_character_png_end");

namespace character_asset {
inline const uint8_t *const png=orange_character_png_start;
inline const std::size_t png_size=orange_character_png_end-orange_character_png_start;
inline constexpr int source_width=1254,source_height=1254;
inline constexpr int width=180,height=180;
// Default centre is the middle of the play area; motion overrides this origin.
inline constexpr int x=90,y=65;
// Measured white-eye centres in the source, scaled to the cached sprite.
inline constexpr float left_eye_x=53.5592f,left_eye_y=55.5325f;
inline constexpr float right_eye_x=126.5762f,right_eye_y=55.5506f;
inline constexpr float pupil_radius=10.125f,max_pupil_x=1.6875f,max_pupil_y=1.125f;
}
