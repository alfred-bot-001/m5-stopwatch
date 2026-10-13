#pragma once
#include <cstddef>
#include <cstdint>

// Display-only transition state: time alone does not complete a fade. The
// renderer acknowledges the final full-target frame before animation stops.
class PetTransition {
public:
 static constexpr uint32_t duration_ms=360;

 void prime(uint8_t target) {target_=target;primed_=true;active_=false;}

 // True asks the renderer to snapshot its last displayed, already mixed frame.
 // A second target change restarts from that snapshot, never from an old asset.
 bool target(uint32_t now,uint8_t next,bool displayed_frame) {
  if(!primed_){prime(next);return false;}
  if(next==target_)return false;
  target_=next;started_at_=now;active_=displayed_frame;
  return active_;
 }

 uint16_t weight(uint32_t now) const {
  if(!active_)return 256;
  const uint32_t elapsed=uint32_t(now-started_at_);
  if(elapsed>=duration_ms)return 256;
  // Q16 smoothstep avoids floating point and is exact at either endpoint.
  const uint32_t t=(elapsed*65536u)/duration_ms;
  const uint64_t curve=uint64_t(t)*t*(196608u-2u*t);
  return uint16_t((curve+(uint64_t(1)<<39))>>40);
 }

 void frame_drawn(uint32_t now) {if(active_ && uint32_t(now-started_at_)>=duration_ms)active_=false;}
 bool active() const {return active_;}
 uint8_t target() const {return target_;}

private:
 uint32_t started_at_=0;
 uint8_t target_=0;
 bool primed_=false,active_=false;
};

namespace pet_transition {
inline constexpr uint16_t swap16(uint16_t value) {
 return uint16_t(uint16_t(value<<8)|uint16_t(value>>8));
}

// LGFX 16-bit sprite getBuffer() contains byte-swapped RGB565 words, not the
// conventional RGB565 values passed to drawPixel. Blend channels after decoding
// that storage format, then restore it for the direct sprite-buffer write.
inline constexpr uint16_t blend_swapped_rgb565(uint16_t from,uint16_t to,uint16_t weight) {
 if(weight==0)return from;
 if(weight>=256)return to;
 const uint16_t a=swap16(from),b=swap16(to);
 const uint32_t inverse=256u-weight;
 const uint32_t red=(((a>>11)&31u)*inverse+((b>>11)&31u)*weight+128u)>>8;
 const uint32_t green=(((a>>5)&63u)*inverse+((b>>5)&63u)*weight+128u)>>8;
 const uint32_t blue=((a&31u)*inverse+(b&31u)*weight+128u)>>8;
 return swap16(uint16_t((red<<11)|(green<<5)|blue));
}

inline void blend_frame(uint16_t *target,const uint16_t *source,std::size_t pixels,uint16_t weight) {
 for(std::size_t i=0;i<pixels;++i)target[i]=blend_swapped_rgb565(source[i],target[i],weight);
}
}
