#include "../main/pet_orientation.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

static float difference(float a,float b){return std::fabs(PetOrientation::delta(a,b));}
static void sample_heading(PetOrientation &state,uint32_t now,float angle,float gyro=0.f) {
 const float radians=angle*0.0174532925199f;
 state.sample(now,-std::sin(radians),std::cos(radians),0.f,gyro);
 const float value=state.advance(now);
 assert(std::isfinite(value)&&value>=-180.f&&value<180.f);
}
int main(){
 // The pet's local head (0,-1) must point opposite screen-space gravity.
 const float cardinal_angles[]={0.f,90.f,180.f,270.f};
 for(const float target:cardinal_angles){
  PetOrientation state;
  for(uint32_t t=0;t<=2000;t+=20){
   sample_heading(state,t,target);
   if(t==80 && target!=0.f)assert(difference(state.angle(),target)>10.f);
   if(t==700)assert(difference(state.angle(),target)<2.f);
  }
  assert(difference(state.angle(),target)<0.01f&&!state.active());
  const float radians=state.angle()*0.0174532925199f;
  const float tx=std::sin(radians),ty=-std::cos(radians);
  const float expected_x=std::sin(target*0.0174532925199f);
  const float expected_y=-std::cos(target*0.0174532925199f);
  assert(std::fabs(tx-expected_x)<0.001f&&std::fabs(ty-expected_y)<0.001f);
 }
 // Continuous rotation crosses every quadrant and follows the shortest path
 // through +/-180, instead of making a nearly complete revolution backwards.
 PetOrientation crossing;
 for(uint32_t t=0;t<=2000;t+=20)sample_heading(crossing,t,179.f);
 float travelled=0.f,previous=crossing.angle();
 for(uint32_t t=2020;t<=3000;t+=20){
  sample_heading(crossing,t,-179.f);
  const float step=PetOrientation::delta(crossing.angle(),previous);
  assert(step>=-0.001f&&step<1.f);travelled+=step;previous=crossing.angle();
 }
 assert(travelled>1.99f&&travelled<2.01f&&!crossing.active());
 PetOrientation turn;
 float last=0.f;
 for(uint32_t t=0;t<12000;t+=20){
  const float heading=std::fmin(540.f,t*0.06f);
  sample_heading(turn,t,heading,60.f);
  assert(PetOrientation::delta(turn.angle(),last)>-0.001f);
  last=turn.angle();
 }
 assert(difference(turn.angle(),180.f)<0.01f&&!turn.active());
 // A flat screen does not supply a usable up direction. Noise and rotation
 // about its Z axis must retain the most recently accepted vertical pose.
 const float flat_heading=turn.angle();
 for(uint32_t t=12000;t<20000;t+=20){
  const float noise=t%40u?0.01f:-0.01f;
  turn.sample(t,noise,-noise,1.f,50.f);turn.advance(t);
  assert(difference(turn.angle(),flat_heading)<0.001f&&!turn.active());
 }
 // Small stationary noise cannot keep a tilted pet animating. A sudden 1 g
 // direction glitch must be confirmed, while a 4 g collision is never gravity.
 PetOrientation quiet;
 for(uint32_t t=0;t<2000;t+=20)sample_heading(quiet,t,90.f);
 for(uint32_t t=2000;t<10000;t+=20){
  const float noise=t%40u?0.005f:-0.005f;
  quiet.sample(t,-1.f+noise,noise,noise,0.2f);quiet.advance(t);
  assert(difference(quiet.angle(),90.f)<0.001f&&!quiet.active());
 }
 sample_heading(quiet,10000,-90.f);
 sample_heading(quiet,10020,90.f);
 assert(difference(quiet.angle(),90.f)<0.001f&&!quiet.active());
 for(uint32_t t=10040;t<12000;t+=20){
  quiet.sample(t,4.f,-4.f,0.f,600.f);quiet.advance(t);
  assert(difference(quiet.angle(),90.f)<0.001f&&!quiet.active());
 }
 // Invalid measurements, free-fall and very stale plausible samples neither
 // erase the last reliable heading nor manufacture animation.
 quiet.sample(12000,std::numeric_limits<float>::quiet_NaN(),0.f,1.f,0.f);
 quiet.advance(12000);
 quiet.sample(12020,0.f,0.f,0.f,0.f);quiet.advance(12020);
 assert(difference(quiet.angle(),90.f)<0.001f&&!quiet.active());
 for(uint32_t t=12500;t<18000;t+=500){
  sample_heading(quiet,t,0.f);assert(difference(quiet.angle(),90.f)<0.001f&&!quiet.active());
 }
 // Rotation can resume once fresh, reliable gravity returns.
 for(uint32_t t=18000;t<20000;t+=20)sample_heading(quiet,t,0.f);
 assert(difference(quiet.angle(),0.f)<0.001f&&!quiet.active());
 // A busy screen can delay fresh IMU updates into the 180-250 ms range. Such
 // samples are still within PetMotion's 300 ms freshness window: confirm the
 // new direction and converge smoothly, while the 500 ms stale case above
 // remains unaccepted. Individual updates must never snap straight to 90.
 PetOrientation busy;
 sample_heading(busy,0,0.f);
 uint32_t busy_now=0;
 const uint32_t busy_gaps[]={180u,220u,250u};
 for(unsigned i=0;i<24;++i){
  busy_now+=busy_gaps[i%3];
  const float previous=busy.angle();
  sample_heading(busy,busy_now,90.f);
  if(i==0)assert(difference(busy.angle(),0.f)<0.001f);
  if(i==1)assert(busy.angle()>0.f&&busy.angle()<80.f);
  assert(busy.angle()>=previous&&busy.angle()<=90.f);
 }
 assert(difference(busy.angle(),90.f)<0.001f&&!busy.active());
 // Both sample confirmation and interpolation remain identical on timer wrap.
 PetOrientation ordinary,wrapped;
 for(uint32_t t=0;t<14000;t+=20){
  const float heading=std::sin(t*0.0005f)*160.f;
  sample_heading(ordinary,t,heading,20.f);sample_heading(wrapped,0xfffff000u+t,heading,20.f);
  assert(difference(ordinary.angle(),wrapped.angle())<0.001f);
  assert(ordinary.active()==wrapped.active());
 }
 std::puts("pet orientation: cardinal directions, smooth shortest rotation, flat hold, quiet settling, shock rejection, busy-frame freshness, stale data and wrap passed");
}
