#include "../main/pet_transition.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

static uint16_t rgb(uint16_t normal) {return pet_transition::swap16(normal);}
int main() {
 using pet_transition::blend_swapped_rgb565;
 // Check actual swapped buffer words, distinct channel widths and exact ends.
 assert(rgb(0xf800)==0x00f8);
 assert(rgb(0x07e0)==0xe007);
 assert(rgb(0x001f)==0x1f00);
 assert(blend_swapped_rgb565(rgb(0xf800),rgb(0x001f),0)==rgb(0xf800));
 assert(blend_swapped_rgb565(rgb(0xf800),rgb(0x001f),256)==rgb(0x001f));
 assert(blend_swapped_rgb565(rgb(0xf800),rgb(0x001f),128)==rgb(0x8010));
 assert(blend_swapped_rgb565(rgb(0x0000),rgb(0xffff),128)==rgb(0x8410));
 assert(blend_swapped_rgb565(rgb(0x07e0),rgb(0x0000),128)==rgb(0x0400));
 assert(blend_swapped_rgb565(rgb(0xaaaa),rgb(0x5555),999)==rgb(0x5555));
 for(unsigned color=0;color<65536;++color){
  const auto sample=uint16_t(color);
  for(unsigned w:{0u,1u,127u,128u,255u,256u})
   assert(blend_swapped_rgb565(sample,sample,uint16_t(w))==sample);
 }
 // Every channel is monotonic and bounded, with red/blue kept independent.
 uint16_t previous_r=0,previous_g=0,previous_b=0;
 for(unsigned w=0;w<=256;++w){
  const auto out=rgb(blend_swapped_rgb565(rgb(0),rgb(0xffff),uint16_t(w)));
  assert((out>>11)>=previous_r);previous_r=out>>11;
  assert(((out>>5)&63u)>=previous_g);previous_g=(out>>5)&63u;
  assert((out&31u)>=previous_b);previous_b=out&31u;
  assert((rgb(blend_swapped_rgb565(rgb(0xf800),rgb(0x001f),uint16_t(w)))&0x07e0)==0);
 }

 PetTransition transition;
 assert(!transition.target(0,0,false));
 assert(!transition.active());assert(transition.weight(0)==256);
 assert(transition.target(100,1,true));assert(transition.active());
 assert(transition.weight(100)==0);assert(transition.weight(280)==128);
 uint16_t previous=0;
 for(uint32_t t=100;t<=460;++t){
  assert(transition.weight(t)>=previous);previous=transition.weight(t);
 }
 assert(transition.weight(460)==256);
 // Even after the deadline, keep requesting paints until the final one is drawn.
 assert(transition.active());transition.frame_drawn(459);assert(transition.active());
 transition.frame_drawn(460);assert(!transition.active());
 assert(!transition.target(1000,1,true));assert(!transition.active());

 // A target interruption snapshots the visible blended color and starts at that
 // exact value; it must not jump back to the previous source or forward target.
 std::array<uint16_t,4> visible={rgb(0xf800),rgb(0x07e0),rgb(0x001f),rgb(0xffff)};
 auto source=visible;
 assert(transition.target(2000,2,true));
 visible={rgb(0x001f),rgb(0x001f),rgb(0xf800),rgb(0)};
 pet_transition::blend_frame(visible.data(),source.data(),visible.size(),transition.weight(2180));
 const auto mid=visible;
 assert(mid!=source);
 assert(transition.target(2180,3,true));source=visible;
 visible={rgb(0x07e0),rgb(0xf800),rgb(0xffff),rgb(0x07e0)};
 pet_transition::blend_frame(visible.data(),source.data(),visible.size(),transition.weight(2180));
 assert(visible==mid);
 assert(transition.target(2181,4,true));source=visible;
 visible.fill(rgb(0));
 pet_transition::blend_frame(visible.data(),source.data(),visible.size(),transition.weight(2181));
 assert(visible==mid);
 transition.frame_drawn(3000);assert(!transition.active());

 transition.prime(4);
 assert(transition.target(0xfffffff0u,5,true));
 assert(transition.weight(0x000000a4u)==128);
 transition.frame_drawn(0x00000158u);assert(!transition.active());
 assert(transition.target(100,6,true));
 assert(!transition.target(110,7,false));assert(!transition.active());
 puts("pet transition: swapped RGB565 colors, endpoints, monotonic easing, interrupted fades, final frame and wrap passed");
}
