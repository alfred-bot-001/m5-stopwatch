#include "../main/battery_ring.hpp"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <vector>

struct Canvas {
 std::vector<uint16_t> data=std::vector<uint16_t>(PET_SCREEN_SIZE*PET_SCREEN_SIZE,0);
 int depth=16;
 int getColorDepth() const{return depth;}
 int width() const{return PET_SCREEN_SIZE;}
 int height() const{return PET_SCREEN_SIZE;}
 void *getBuffer(){return data.data();}
};

int main() {
 Canvas canvas;BatteryRing ring;
 assert(!ring.draw(canvas,50));
 assert(ring.begin() && ring.begin());
 assert(ring.draw(canvas,0));const auto red=canvas.data;
 assert(ring.draw(canvas,100));const auto green=canvas.data;
 assert(ring.draw(canvas,-1));const auto unknown=canvas.data;
 size_t ring_pixels=0;
 for(int y=0;y<PET_SCREEN_SIZE;++y)for(int x=0;x<PET_SCREEN_SIZE;++x){
  const size_t i=y*PET_SCREEN_SIZE+x;
  const float dx=x-PET_SCREEN_CENTER,dy=y-PET_SCREEN_CENTER,r=std::sqrt(dx*dx+dy*dy);
  if(r<227.5f || r>232.5f)assert(red[i]==0 && green[i]==0 && unknown[i]==0);
  if(r>=228.5f && r<=231.5f){
   assert(red[i]!=0 && green[i]!=0 && unknown[i]!=0);
   assert(unknown[i]!=red[i] && unknown[i]!=green[i]);
  }
  if(red[i]!=green[i])++ring_pixels;
 }
 assert(ring_pixels>6500 && ring_pixels<8000);
 for(int percentage: {0,1,25,50,75,99,100}){
  assert(ring.draw(canvas,percentage));size_t green_pixels=0;
  for(size_t i=0;i<red.size();++i){
   assert(canvas.data[i]==red[i] || canvas.data[i]==green[i]);
   if(red[i]!=green[i] && canvas.data[i]==green[i])++green_pixels;
  }
  assert(std::fabs(float(green_pixels)/ring_pixels-percentage/100.f)<0.002f);
 }
 // The first quarter must extend from the top to the right, not the left.
 assert(ring.draw(canvas,25));
 assert(canvas.data[3*466+233]==green[3*466+233]);
 assert(canvas.data[70*466+396]==green[70*466+396]);
 assert(canvas.data[70*466+70]==red[70*466+70]);
 assert(canvas.data[396*466+396]==red[396*466+396]);
 // Endpoint clamping and invalid canvas formats cannot create opposite seams.
 assert(ring.draw(canvas,101));assert(canvas.data==green);
 assert(ring.draw(canvas,-20));assert(canvas.data==unknown);
 canvas.depth=8;assert(!ring.draw(canvas,50));assert(canvas.data==unknown);
 canvas.depth=16;std::fill(canvas.data.begin(),canvas.data.end(),0x1234);
 assert(ring.draw(canvas,50));
 for(int y=0;y<466;++y)for(int x=0;x<466;++x){
  const float dx=x-233.f,dy=y-233.f,r=std::sqrt(dx*dx+dy*dy);
  if(r<227.5f || r>232.5f)assert(canvas.data[y*466+x]==0x1234);
 }
 std::printf("Battery ring geometry/color tests passed (%zu pixels).\n",ring_pixels);
}
