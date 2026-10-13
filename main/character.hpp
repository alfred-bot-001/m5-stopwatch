#pragma once

#include <M5Unified.h>
#include <cmath>
#include <cstring>
#include "character_asset.hpp"
#include "pet_expression.hpp"
#include "pet_motion.hpp"

// Decode each supplied design once. All expressions use the same body scale,
// anchor and collision envelope; switching a face never resizes the pet.
class CharacterRenderer {
public:
 bool begin() {
  bool all_ready=true;
  for(unsigned i=0;i<character_asset::Count;++i){
   auto &sprite=backgrounds_[i];
   sprite.setColorDepth(16);sprite.setPsram(true);
   if(sprite.createSprite(character_asset::width,character_asset::height)){
    sprite.fillSprite(TFT_BLACK);
    const auto &asset=character_asset::assets[i];
    // Sleepy's body starts at source row 74. Crop the original upper Zs during
    // composition, then place smaller Zs inside our round-screen-safe envelope.
    sprite.setClipRect(character_asset::body_x,character_asset::body_y,
     character_asset::body_width,character_asset::body_height);
    const int top=character_asset::body_y-int(std::lround(asset.source_top*character_asset::scale));
    ready_[i]=sprite.drawPng(asset.data,asset.size,character_asset::body_x,top,
     character_asset::width-character_asset::body_x,character_asset::height-top,
     0,0,character_asset::scale,character_asset::scale);
    sprite.clearClipRect();sprite.releasePngMemory();
    if(ready_[i] && i==character_asset::Sleepy)sleep_z(sprite);
   }
   if(!ready_[i]){sprite.deleteSprite();all_ready=false;}
  }
  talking_.setColorDepth(16);talking_.setPsram(true);
  talking_ready_=ready_[character_asset::Happy] && talking_.createSprite(character_asset::width,character_asset::height);
  if(talking_ready_){
   copy(talking_,backgrounds_[character_asset::Happy]);
   clear_mouth_line(talking_);
  }
  frame_.setColorDepth(16);frame_.setPsram(true);
  frame_ready_=frame_.createSprite(character_asset::width,character_asset::height)!=nullptr;
  if(frame_ready_)frame_.setPivot(character_asset::width*0.5f,character_asset::height*0.5f);
  return all_ready && talking_ready_ && frame_ready_;
 }

 void update(uint32_t now,bool listening,float rms,const PetMotionView &motion,uint32_t idle_ms) {
  expression_.update(now,listening,rms,motion,idle_ms);
  if(!started_){last_update_=now;started_=true;return;}
  const float dt=std::fmin(0.25f,uint32_t(now-last_update_)*0.001f);last_update_=now;
  if(motion.dizzy>0.03f)decoration_phase_=std::fmod(decoration_phase_+dt*1.8f,6.283185307f);
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

 bool animating() const {return expression_.view().animating || amplitude_!=0.f || x_!=0.f || y_!=0.f;}
 PetExpression expression() const {return expression_.view().expression;}
 float mouth_open() const {return expression_.view().mouth_open;}

 void draw(M5Canvas &canvas,bool,bool,bool,const PetMotionView &motion) {
  if(canvas.getColorDepth()==16 && canvas.getBuffer())
   std::memset(canvas.getBuffer(),0,size_t(canvas.width())*canvas.height()*sizeof(uint16_t));
  else canvas.fillSprite(TFT_BLACK);
  const float dizziness=bounded(motion.dizzy,0.f,1.f);
  const auto face=expression();
  if(frame_ready_){
   const auto asset=asset_index(face);
   const bool talking=face==PetExpression::Talking && mouth_open()>0.025f;
   if(talking && talking_ready_)copy(frame_,talking_);
   else if(ready_[asset])copy(frame_,backgrounds_[asset]);
   else if(ready_[character_asset::Happy])copy(frame_,backgrounds_[character_asset::Happy]);
   else{frame_.fillSprite(TFT_BLACK);draw_fallback(frame_,0,0);}
   // Other designs retain their original eyelids, brow, blush and tear. Only
   // round-eyed happy/talking/dizzy faces need procedural pupil replacement.
   if(face==PetExpression::Happy || face==PetExpression::Talking || face==PetExpression::Dizzy)
    eyes(frame_,0.f,0.f,face==PetExpression::Dizzy?dizziness:0.f);
   if(talking)mouth(frame_,mouth_open());
   const float squash=bounded(motion.squash,0.f,1.f);
   const float angle=bounded(motion.angle,-12.f,12.f);
   if(std::fabs(angle)<0.3f && squash<0.008f){
    const int left=int(std::lround(motion.x-character_asset::width*0.5f));
    const int top=int(std::lround(motion.y-character_asset::height*0.5f));
    if(left>=0 && top>=0 && left+character_asset::width<=canvas.width()
     && top+character_asset::height<=canvas.height() && canvas.getColorDepth()==16){
     auto *dst=static_cast<uint16_t*>(canvas.getBuffer());
     const auto *src=static_cast<const uint16_t*>(frame_.getBuffer());
     for(int row=0;row<character_asset::height;++row)
      std::memcpy(dst+size_t(top+row)*canvas.width()+left,
       src+size_t(row)*character_asset::width,character_asset::width*sizeof(uint16_t));
    }else frame_.pushSprite(&canvas,left,top,TFT_BLACK);
   }else frame_.pushRotateZoom(&canvas,motion.x,motion.y,angle,
     1.f+0.06f*squash,1.f-0.11f*squash,TFT_BLACK);
  }else{
   const int x=int(std::lround(motion.x-character_asset::width*0.5f));
   const int y=int(std::lround(motion.y-character_asset::height*0.5f));
   draw_fallback(canvas,x,y);eyes(canvas,float(x),float(y),dizziness);
  }
  if(dizziness>0.03f)stars(canvas,motion,dizziness);
 }

private:
 M5Canvas backgrounds_[character_asset::Count],talking_,frame_;
 PetExpressionState expression_;
 bool ready_[character_asset::Count]{},talking_ready_=false,frame_ready_=false,started_=false;
 uint32_t last_update_=0;
 float amplitude_=0.f,phase_=0.f,decoration_phase_=0.f,x_=0.f,y_=0.f;

 static void copy(M5Canvas &destination,const M5Canvas &source) {
  std::memcpy(destination.getBuffer(),source.getBuffer(),
   size_t(character_asset::width)*character_asset::height*sizeof(uint16_t));
 }
 static float bounded(float value,float low,float high) {
  return std::isfinite(value)?std::fmin(high,std::fmax(low,value)):low;
 }
 static character_asset::Index asset_index(PetExpression face) {
  using namespace character_asset;
  switch(face){
   case PetExpression::Laugh:return Laugh;
   case PetExpression::Surprised:return Surprised;
   case PetExpression::Shy:return Shy;
   case PetExpression::Sad:return Sad;
   case PetExpression::Angry:return Angry;
   case PetExpression::Sleepy:return Sleepy;
   case PetExpression::Wink:return Wink;
   default:return Happy;
  }
 }

 static void clear_mouth_line(M5Canvas &canvas) {
  // Replace just the approximately 30x9-pixel closed mouth footprint, once at
  // startup. Texture is interpolated from that same column's neighbouring skin,
  // only where the supplied brown line darkens it; no flat face patch is used.
  constexpr int left=104,right=135,top=151,bottom=161;
  for(int x=left;x<=right;++x){
   const auto a=canvas.readPixelRGB(x,top-1),b=canvas.readPixelRGB(x,bottom+1);
   for(int y=top;y<=bottom;++y){
    const auto pixel=canvas.readPixelRGB(x,y);
    const float t=float(y-top+1)/float(bottom-top+2);
    const float red=a.r+(float(b.r)-a.r)*t;
    const float green=a.g+(float(b.g)-a.g)*t;
    const float blue=a.b+(float(b.b)-a.b)*t;
    if(pixel.r+12.f<red && pixel.g+5.f<green)
     canvas.drawPixel(x,y,canvas.color565(uint8_t(red),uint8_t(green),uint8_t(blue)));
   }
  }
 }

 void eyes(M5Canvas &canvas,float x,float y,float dizziness) const {
  for(int eye=0;eye<2;++eye){
   const float cx=x+(eye?character_asset::right_eye_x:character_asset::left_eye_x);
   const float cy=y+character_asset::left_eye_y;
   // Cover the whole old pupil footprint before moving it. The white circle is
   // enlarged by ~1.7 px because the supplied pupils overlap the white edges.
   canvas.drawSpot(cx,cy,character_asset::white_radius,TFT_WHITE);
   if(dizziness>0.03f)spiral(canvas,cx,cy,decoration_phase_+(eye?0.35f:0.f));
   else{
    const float px=cx+(eye?-3.f:3.f)+x_,py=cy+1.5f+y_;
    canvas.drawSpot(px,py,character_asset::pupil_radius,canvas.color565(28,24,21));
    canvas.drawSpot(px+4.f,py-5.f,3.35f,TFT_WHITE);
   }
  }
 }

 static void spiral(M5Canvas &canvas,float cx,float cy,float phase) {
  const uint16_t ink=canvas.color565(48,27,20);
  float previous_x=cx,previous_y=cy;
  for(int i=1;i<=38;++i){
   const float fraction=i/38.f;
   const float angle=phase+fraction*10.9955743f;
   const float radius=character_asset::pupil_radius*0.91f*fraction;
   const float x=cx+std::cos(angle)*radius,y=cy+std::sin(angle)*radius;
   canvas.drawWideLine(previous_x,previous_y,x,y,1.2f,ink);
   previous_x=x;previous_y=y;
  }
 }

 static void mouth(M5Canvas &canvas,float amount) {
  const float open=bounded(amount,0.f,1.f);
  const float cx=character_asset::mouth_x,top=character_asset::mouth_y-3.f;
  const float half_width=14.f+8.f*open,height=5.f+21.f*open;
  // Four samples per pixel keep this tiny mouth smooth. The shape follows the
  // supplied laugh: a shallow curved upper lip, round lower lip and coral tongue.
  for(int y=int(top)-1;y<=int(top+height)+1;++y)for(int x=int(cx-half_width)-1;x<=int(cx+half_width)+1;++x){
   const auto background=canvas.readPixelRGB(x,y);
   unsigned sum_r=0,sum_g=0,sum_b=0;
   bool painted=false;
   for(int sample=0;sample<4;++sample){
    const float px=x+(sample&1?0.75f:0.25f),py=y+(sample&2?0.75f:0.25f);
    const float u=(px-cx)/half_width;
    unsigned r=background.r,g=background.g,b=background.b;
    if(std::fabs(u)<1.f && py>=top+2.5f*(1.f-u*u) && py<=top+height*std::sqrt(1.f-u*u)){
     r=141;g=55;b=33;painted=true;
     const float inner_u=(px-cx)/(half_width-1.1f);
     if(std::fabs(inner_u)<1.f && py>=top+1.1f+2.5f*(1.f-inner_u*inner_u)
      && py<=top+(height-1.1f)*std::sqrt(1.f-inner_u*inner_u)){
      r=119;g=39;b=23;
      const float tx=(px-cx)/(half_width*0.47f),ty=(py-(top+height-1.5f))/(3.f+height*0.16f);
      if(tx*tx+ty*ty<=1.f){r=239;g=130;b=107;}
     }
    }
    sum_r+=r;sum_g+=g;sum_b+=b;
   }
   if(painted)canvas.drawPixel(x,y,canvas.color565(sum_r/4,sum_g/4,sum_b/4));
  }
 }

 static void sleep_z(M5Canvas &canvas) {
  const uint16_t brown=canvas.color565(149,54,31);
  const auto glyph=[&](float x,float y,float size){
   canvas.drawWideLine(x,y,x+size,y,1.15f,brown);
   canvas.drawWideLine(x+size,y,x,y+size*1.25f,1.15f,brown);
   canvas.drawWideLine(x,y+size*1.25f,x+size,y+size*1.25f,1.15f,brown);
  };
  glyph(162.f,34.f,4.f);glyph(175.f,24.f,6.f);
 }

 void stars(M5Canvas &canvas,const PetMotionView &motion,float amount) const {
  const float angle=bounded(motion.angle,-12.f,12.f)*0.0174532925f;
  const float head_x=motion.x+std::sin(angle)*85.f;
  const float head_y=motion.y-std::cos(angle)*85.f;
  // Golden five-point stars remain recognisably gold while fading out.
  const float fade=bounded(amount*2.f,0.f,1.f);
  const uint16_t gold=canvas.color565(uint8_t(255.f*fade),uint8_t(199.f*fade),uint8_t(45.f*fade));
  const uint16_t light=canvas.color565(uint8_t(255.f*fade),uint8_t(228.f*fade),uint8_t(123.f*fade));
  for(int i=0;i<3;++i){
   const float phase=decoration_phase_+i*2.0943951f;
   float sx=head_x+std::cos(phase)*43.f,sy=head_y+std::sin(phase)*9.f;
   const float dx=sx-PET_SCREEN_CENTER,dy=sy-PET_SCREEN_CENTER;
   // Outermost star tip remains at radius 225, inside the ring's radius 228.
   constexpr float star_bound=PET_SCREEN_RADIUS-12.f;
   const float r2=dx*dx+dy*dy;
   if(r2>star_bound*star_bound){
    const float scale=star_bound/std::sqrt(r2);
    sx=PET_SCREEN_CENTER+dx*scale;sy=PET_SCREEN_CENTER+dy*scale;
   }
   float px[10],py[10];
   for(int point=0;point<10;++point){
    const float direction=-1.5707963f+point*0.62831853f+0.1f*std::sin(phase);
    const float radius=point&1?2.5f:5.8f;
    px[point]=sx+std::cos(direction)*radius;py[point]=sy+std::sin(direction)*radius;
   }
   for(int point=0;point<10;++point){
    const int next=(point+1)%10;
    canvas.fillTriangle(int(std::lround(sx)),int(std::lround(sy)),
     int(std::lround(px[point])),int(std::lround(py[point])),
     int(std::lround(px[next])),int(std::lround(py[next])),point<4?light:gold);
   }
  }
 }

 static void draw_fallback(M5Canvas &canvas,int origin_x,int origin_y) {
  const int cx=origin_x+character_asset::width/2;
  const int cy=origin_y+character_asset::height/2;
  const uint16_t orange=canvas.color565(237,115,70);
  canvas.fillEllipse(cx,cy+character_asset::height/8,character_asset::width*2/5,character_asset::height/3,orange);
  for(int eye=0;eye<2;++eye){
   const float x=origin_x+(eye?character_asset::right_eye_x:character_asset::left_eye_x);
   const float y=origin_y+character_asset::left_eye_y;
   canvas.drawSpot(x,y,character_asset::white_radius*1.7f,orange);
   canvas.drawSpot(x,y,character_asset::white_radius,TFT_WHITE);
  }
 }
};
