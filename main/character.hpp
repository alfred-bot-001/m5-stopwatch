#pragma once

#include <M5Unified.h>
#include <cmath>
#include "character_asset.hpp"
#include "pet_motion.hpp"

// The textured body is decoded once into PSRAM. A second small sprite composes
// its animated eyes before the whole pet moves, tilts, and rebounds as one.
class CharacterRenderer {
public:
 bool begin() {
  background_.setColorDepth(16);background_.setPsram(true);
  if(background_.createSprite(character_asset::width,character_asset::height)){
   background_.fillSprite(TFT_BLACK);
   cached_=background_.drawPng(character_asset::png,character_asset::png_size,0,0,
    character_asset::width,character_asset::height,0,0,
    float(character_asset::width)/character_asset::source_width,
    float(character_asset::height)/character_asset::source_height);
   background_.releasePngMemory();
  }
  if(!cached_)background_.deleteSprite();
  frame_.setColorDepth(16);frame_.setPsram(true);
  frame_ready_=frame_.createSprite(character_asset::width,character_asset::height)!=nullptr;
  if(frame_ready_)frame_.setPivot(character_asset::width*0.5f,character_asset::height*0.5f);
  return cached_ && frame_ready_;
 }

 void update(uint32_t now,bool listening,float rms) {
  if(!started_){last_update_=now;started_=true;return;}
  const float dt=std::fmin(0.25f,uint32_t(now-last_update_)*0.001f);last_update_=now;
  decoration_phase_=std::fmod(decoration_phase_+dt*1.8f,6.283185307f);
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

 void draw(M5Canvas &canvas,bool held,bool listening,bool mic_ok,const PetMotionView &motion) {
  canvas.fillSprite(TFT_BLACK);
  const float dizziness=bounded(motion.dizzy,0.f,1.f);
  const float impact=bounded(motion.impact,0.f,1.f);
  const uint16_t fence=canvas.color565(uint8_t(25.f+impact*21.f),uint8_t(50.f+impact*35.f),uint8_t(51.f+impact*24.f));
  // The frame moves as one sprite, so body, eyes, and impact squashing agree.
  canvas.setClipRect(14,14,332,282);
  if(frame_ready_){
   frame_.fillSprite(TFT_BLACK);
   if(cached_)background_.pushSprite(&frame_,0,0);
   else draw_fallback(frame_,0,0);
   eyes(frame_,0.f,0.f,dizziness);
   const float squash=bounded(motion.squash,0.f,1.f);
   const float angle=bounded(motion.angle,-12.f,12.f);
   // A whole-sprite AA transform blends every pixel through an ARGB buffer.
   // Keep antialiasing on the tiny pupils, and use cheap copies when upright.
   if(std::fabs(angle)<0.3f && squash<0.008f){
    frame_.pushSprite(&canvas,int(std::lround(motion.x-character_asset::width*0.5f)),
     int(std::lround(motion.y-character_asset::height*0.5f)),TFT_BLACK);
   }else{
    frame_.pushRotateZoom(&canvas,motion.x,motion.y,angle,
     1.f+0.06f*squash,1.f-0.11f*squash,TFT_BLACK);
   }
  }else{
   const int x=int(std::lround(motion.x-character_asset::width*0.5f));
   const int y=int(std::lround(motion.y-character_asset::height*0.5f));
   draw_fallback(canvas,x,y);eyes(canvas,float(x),float(y),dizziness);
  }
  if(dizziness>0.03f)stars(canvas,motion,dizziness);
  canvas.clearClipRect();
  mask_corners(canvas);
  canvas.drawRoundRect(12,12,336,286,24,fence);
  // A quiet dot preserves yellow-key feedback without tinting the character.
  const uint16_t dot=!mic_ok?TFT_RED:(held?canvas.color565(255,181,54)
   :(listening?TFT_CYAN:canvas.color565(32,79,76)));
  canvas.drawSpot(180,304,3.0f+amplitude_*0.7f,dot);
 }

private:
 M5Canvas background_,frame_;
 bool cached_=false,frame_ready_=false,started_=false;
 uint32_t last_update_=0;
 float amplitude_=0.f,phase_=0.f,decoration_phase_=0.f,x_=0.f,y_=0.f;

 static float bounded(float value,float low,float high) {
  return std::isfinite(value)?std::fmin(high,std::fmax(low,value)):low;
 }

 static void mask_corners(M5Canvas &canvas) {
  // The rectangular graphics clip protects the footer; this tiny mask also
  // keeps the moving sprite inside the arena's rounded inner corners.
  for(int y=1;y<=22;++y)for(int x=1;x<=22;++x){
   if(x*x+y*y<=22*22)continue;
   canvas.drawPixel(36-x,36-y,TFT_BLACK);canvas.drawPixel(323+x,36-y,TFT_BLACK);
   canvas.drawPixel(36-x,273+y,TFT_BLACK);canvas.drawPixel(323+x,273+y,TFT_BLACK);
  }
 }

 void eyes(M5Canvas &canvas,float x,float y,float dizziness) const {
  const float opacity=(1.f-dizziness)*(1.f-dizziness);
  for(int eye=0;eye<2;++eye){
   const float cx=x+(eye?character_asset::right_eye_x:character_asset::left_eye_x);
   const float cy=y+(eye?character_asset::right_eye_y:character_asset::left_eye_y);
   pupil(canvas,cx+x_*(1.f-dizziness),cy+y_*(1.f-dizziness),character_asset::pupil_radius,opacity);
   if(dizziness>0.03f)spiral(canvas,cx,cy,dizziness,decoration_phase_+(eye?0.35f:0.f));
  }
 }

 static void spiral(M5Canvas &canvas,float cx,float cy,float amount,float phase) {
  const uint16_t ink=canvas.color565(uint8_t(255.f-233.f*amount),uint8_t(255.f-228.f*amount),uint8_t(255.f-230.f*amount));
  float previous_x=cx,previous_y=cy;
  for(int i=1;i<=32;++i){
   const float fraction=i/32.f;
   const float angle=phase+fraction*10.9955743f;
   const float radius=character_asset::pupil_radius*0.91f*fraction;
   const float x=cx+std::cos(angle)*radius,y=cy+std::sin(angle)*radius;
   canvas.drawWideLine(int(std::lround(previous_x)),int(std::lround(previous_y)),int(std::lround(x)),int(std::lround(y)),0.7f,ink);
   previous_x=x;previous_y=y;
  }
 }

 void stars(M5Canvas &canvas,const PetMotionView &motion,float amount) const {
  const float angle=bounded(motion.angle,-12.f,12.f)*0.0174532925f;
  const float head_x=motion.x+std::sin(angle)*62.f;
  const float head_y=motion.y-std::cos(angle)*62.f;
  const uint16_t color=canvas.color565(uint8_t(238.f*amount),uint8_t(193.f*amount),uint8_t(79.f*amount));
  for(int i=0;i<3;++i){
   const float phase=decoration_phase_+i*2.0943951f;
   const int x=int(std::lround(bounded(head_x+std::cos(phase)*40.f,21.f,339.f)));
   const int y=int(std::lround(bounded(head_y+std::sin(phase)*9.f,21.f,289.f)));
   canvas.drawWideLine(x-3,y,x+3,y,0.6f,color);
   canvas.drawWideLine(x,y-3,x,y+3,0.6f,color);
   canvas.drawSpot(x,y,1.0f,color);
  }
 }

 static void pupil(M5Canvas &canvas,float cx,float cy,float radius,float opacity=1.f) {
  if(opacity<0.001f)return;
  const int left=int(std::floor(cx-radius-1.f)),right=int(std::ceil(cx+radius+1.f));
  const int top=int(std::floor(cy-radius-1.f)),bottom=int(std::ceil(cy+radius+1.f));
  const float inner=(radius-0.5f)*(radius-0.5f),outer=(radius+0.5f)*(radius+0.5f);
  const uint16_t ink=canvas.color565(22,27,25);
  for(int py=top;py<=bottom;++py)for(int px=left;px<=right;++px){
   const float dx=px-cx,dy=py-cy,d2=dx*dx+dy*dy;
   if(d2>=outer)continue;
   if(d2<=inner && opacity>0.999f){canvas.drawPixel(px,py,ink);continue;}
   const float alpha=(d2<=inner?1.f:radius+0.5f-std::sqrt(d2))*opacity;
   const auto bg=canvas.readPixelRGB(px,py);
   canvas.drawPixel(px,py,canvas.color565(
    uint8_t(bg.r+(22.f-bg.r)*alpha),uint8_t(bg.g+(27.f-bg.g)*alpha),uint8_t(bg.b+(25.f-bg.b)*alpha)));
  }
 }

 static void draw_fallback(M5Canvas &canvas,int origin_x,int origin_y) {
  const int cx=origin_x+character_asset::width/2;
  const int cy=origin_y+character_asset::height/2;
  const uint16_t orange=canvas.color565(255,112,64);
  canvas.fillEllipse(cx,cy+character_asset::height/8,character_asset::width*2/5,character_asset::height/3,orange);
  const float white_radius=character_asset::pupil_radius+std::fmax(character_asset::max_pupil_x,character_asset::max_pupil_y)+2.5f;
  for(int eye=0;eye<2;++eye){
   const int x=int(std::lround(origin_x+(eye?character_asset::right_eye_x:character_asset::left_eye_x)));
   const int y=int(std::lround(origin_y+(eye?character_asset::right_eye_y:character_asset::left_eye_y)));
   canvas.drawSpot(x,y,white_radius*1.7f,orange);
   canvas.drawSpot(x,y,white_radius,TFT_WHITE);
  }
 }
};
