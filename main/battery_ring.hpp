#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include "pet_layout.h"
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

// Four-pixel ring at the physical panel edge. Cache geometry and colors once;
// redrawing after the pet clears its canvas requires no trig or pixel reads.
class BatteryRing {
public:
 static constexpr float inner_radius=228.f,outer_radius=232.f;
 BatteryRing()=default;
 BatteryRing(const BatteryRing&)=delete;
 BatteryRing& operator=(const BatteryRing&)=delete;
 ~BatteryRing(){std::free(pixels_);}

 bool begin() {
  if(pixels_)return true;
  size_t count=0;
  for(int y=0;y<PET_SCREEN_SIZE;++y)for(int x=0;x<PET_SCREEN_SIZE;++x)
   if(in_band(x,y))++count;
#ifdef ESP_PLATFORM
  pixels_=static_cast<Pixel*>(heap_caps_malloc(count*sizeof(Pixel),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!pixels_)pixels_=static_cast<Pixel*>(heap_caps_malloc(count*sizeof(Pixel),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
#else
  pixels_=static_cast<Pixel*>(std::malloc(count*sizeof(Pixel)));
#endif
  if(!pixels_)return false; // Caller reports an unavailable ring, not 0%.
  constexpr float turn=6.283185307179586f;
  for(int y=0;y<PET_SCREEN_SIZE;++y)for(int x=0;x<PET_SCREEN_SIZE;++x){
   if(!in_band(x,y))continue;
   const float dx=x-PET_SCREEN_CENTER,dy=y-PET_SCREEN_CENTER;
   const float radius=std::sqrt(dx*dx+dy*dy);
   const float coverage=std::fmin(1.f,std::fmin(radius-inner_radius+0.5f,outer_radius+0.5f-radius));
   float angle=std::atan2(dx,-dy); // Start at 12 o'clock, clockwise.
   if(angle<0.f)angle+=turn;
   const uint32_t position=uint32_t(angle*(65536.f/turn));
   pixels_[count_++]={uint32_t(y*PET_SCREEN_SIZE+x),uint16_t(position>65535?65535:position),
    native_color(40,224,96,coverage),native_color(245,65,65,coverage),native_color(95,105,115,coverage)};
  }
  return true;
 }

 // M5Canvas uses byte-swapped RGB565 for its 16-bit sprite. A template keeps
 // this renderer testable on a host without bringing in the ESP display SDK.
 // Call after CharacterRenderer::draw(); negative percentages mean unknown.
 template<class Canvas> bool draw(Canvas &canvas,int percent) const {
  if(!pixels_ || canvas.getColorDepth()!=16 || canvas.width()!=PET_SCREEN_SIZE
   || canvas.height()!=PET_SCREEN_SIZE || !canvas.getBuffer())return false;
  auto *buffer=static_cast<uint16_t*>(canvas.getBuffer());
  const uint32_t cutoff=percent>0 && percent<100?uint32_t(percent)*65536u/100u:0;
  for(size_t i=0;i<count_;++i){
   const Pixel &p=pixels_[i];
   buffer[p.offset]=percent<0?p.gray:(percent>=100 || (percent>0 && p.angle<cutoff)?p.green:p.red);
  }
  return true;
 }

private:
 struct Pixel {uint32_t offset;uint16_t angle,green,red,gray;};
 Pixel *pixels_=nullptr;
 size_t count_=0;

 static bool in_band(int x,int y) {
  const float dx=x-PET_SCREEN_CENTER,dy=y-PET_SCREEN_CENTER,r2=dx*dx+dy*dy;
  return r2>(inner_radius-0.5f)*(inner_radius-0.5f)
   && r2<(outer_radius+0.5f)*(outer_radius+0.5f);
 }

 static uint16_t native_color(uint8_t r,uint8_t g,uint8_t b,float coverage) {
  const uint16_t rgb=(uint16_t(r*coverage)>>3)<<11 | (uint16_t(g*coverage)>>2)<<5 | (uint16_t(b*coverage)>>3);
  return uint16_t((rgb<<8)|(rgb>>8));
 }
};
