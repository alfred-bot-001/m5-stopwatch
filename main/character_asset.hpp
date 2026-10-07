#pragma once
#include <cstddef>
#include <cstdint>

extern const uint8_t orange_character_png_start[] asm("_binary_orange_character_png_start");
extern const uint8_t orange_character_png_end[] asm("_binary_orange_character_png_end");

namespace character_asset {
inline const uint8_t *const png=orange_character_png_start;
inline const std::size_t png_size=orange_character_png_end-orange_character_png_start;
inline constexpr int source_width=1254,source_height=1254;
inline constexpr int width=320,height=320,x=20,y=5;
// Measured white-eye centres in the source, scaled to the cached sprite.
inline constexpr float left_eye_x=95.2163f,left_eye_y=98.7244f;
inline constexpr float right_eye_x=225.0244f,right_eye_y=98.7567f;
inline constexpr float pupil_radius=18.f,max_pupil_x=3.f,max_pupil_y=2.f;
}
