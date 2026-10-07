#pragma once

#include <M5Unified.h>
#include <cmath>
#include "character_asset.hpp"

// The textured body is decoded once into PSRAM. Only the pupils and tiny
// listening indicator change between frames; neither affects the input path.
class CharacterRenderer {
public:
 bool begin() {
  background_.setColorDepth(16);background_.setPsram(true);
  if(!background_.createSprite(character_asset::width,character_asset::height))return false;
  background_.fillSprite(TFT_BLACK);
  cached_=background_.drawPng(character_asset::png,character_asset::png_size,0,0,
   character_asset::width,character_asset::height,0,0,
   float(character_asset::width)/character_asset::source_width,
   float(character_asset::height)/character_asset::source_height);
  background_.releasePngMemory();
  if(!cached_)background_.deleteSprite();
  return cached_;
 }

 void update(uint32_t now,bool listening,float rms) {
  if(!started_){last_update_=now;started_=true;return;}
  const float dt=std::fmin(0.25f,uint32_t(now-last_update_)*0.001f);last_update_=now;
  const float signal=listening && std::isfinite(rms)
   ?std::fmin(1.f,std::fmax(0.f,(rms-0.006f)*18.f)):0.f;
  const float tau=signal>amplitude_?0.07f:0.20f;
  amplitude_+=(signal-amplitude_)*(1.f-std::exp(-dt/tau));
  if(signal>0.f)phase_=std::fmod(phase_+dt*1.6f,6.283185307f);
  if(signal==0.f && amplitude_<0.004f)amplitude_=0.f;
  const float target_x=character_asset::max_pupil_x*amplitude_*std::cos(phase_);
  const float target_y=character_asset::max_pupil_y*amplitude_*std::sin(phase_);
  const float easing=1.f-std::exp(-dt/0.09f);
  x_+=(target_x-x_)*easing;y_+=(target_y-y_)*easing;
  if(amplitude_==0.f && std::fabs(x_)<0.01f && std::fabs(y_)<0.01f){x_=y_=phase_=0.f;}
 }

 void draw(M5Canvas &canvas,bool held,bool listening,bool mic_ok) {
  canvas.fillSprite(TFT_BLACK);
  if(cached_)background_.pushSprite(&canvas,character_asset::x,character_asset::y);
  else draw_fallback(canvas);
  pupil(canvas,character_asset::x+character_asset::left_eye_x+x_,
   character_asset::y+character_asset::left_eye_y+y_,character_asset::pupil_radius);
  pupil(canvas,character_asset::x+character_asset::right_eye_x+x_,
   character_asset::y+character_asset::right_eye_y+y_,character_asset::pupil_radius);
  // A quiet dot preserves yellow-key feedback without tinting the character.
  const uint16_t dot=!mic_ok?TFT_RED:(held?canvas.color565(255,181,54)
   :(listening?TFT_CYAN:canvas.color565(32,79,76)));
  canvas.drawSpot(180,292,3.0f+amplitude_*0.7f,dot);
 }

private:
 M5Canvas background_;
 bool cached_=false,started_=false;
 uint32_t last_update_=0;
 float amplitude_=0.f,phase_=0.f,x_=0.f,y_=0.f;

 static void pupil(M5Canvas &canvas,float cx,float cy,float radius) {
  const int left=int(std::floor(cx-radius-1.f)),right=int(std::ceil(cx+radius+1.f));
  const int top=int(std::floor(cy-radius-1.f)),bottom=int(std::ceil(cy+radius+1.f));
  const float inner=(radius-0.5f)*(radius-0.5f),outer=(radius+0.5f)*(radius+0.5f);
  const uint16_t ink=canvas.color565(22,27,25);
  for(int py=top;py<=bottom;++py)for(int px=left;px<=right;++px){
   const float dx=px-cx,dy=py-cy,d2=dx*dx+dy*dy;
   if(d2>=outer)continue;
   if(d2<=inner){canvas.drawPixel(px,py,ink);continue;}
   const float alpha=radius+0.5f-std::sqrt(d2);
   const auto bg=canvas.readPixelRGB(px,py);
   canvas.drawPixel(px,py,canvas.color565(
    uint8_t(bg.r+(22.f-bg.r)*alpha),uint8_t(bg.g+(27.f-bg.g)*alpha),uint8_t(bg.b+(25.f-bg.b)*alpha)));
  }
 }

 static void draw_fallback(M5Canvas &canvas) {
  const int cx=character_asset::x+character_asset::width/2;
  const int cy=character_asset::y+character_asset::height/2;
  const uint16_t orange=canvas.color565(255,112,64);
  canvas.fillEllipse(cx,cy+character_asset::height/8,character_asset::width*2/5,character_asset::height/3,orange);
  const float white_radius=character_asset::pupil_radius+std::fmax(character_asset::max_pupil_x,character_asset::max_pupil_y)+2.5f;
  for(int eye=0;eye<2;++eye){
   const int x=int(std::lround(character_asset::x+(eye?character_asset::right_eye_x:character_asset::left_eye_x)));
   const int y=int(std::lround(character_asset::y+(eye?character_asset::right_eye_y:character_asset::left_eye_y)));
   canvas.drawSpot(x,y,white_radius*1.7f,orange);
   canvas.drawSpot(x,y,white_radius,TFT_WHITE);
  }
 }
};
