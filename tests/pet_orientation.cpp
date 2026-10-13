#include "../main/pet_orientation.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>

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
  for(uint32_t t=0;t<=3000;t+=20){
   sample_heading(state,t,target);
   if(t==80 && target!=0.f)assert(difference(state.angle(),target)>10.f);
   assert(std::fabs(state.angular_velocity())<=260.f);
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
  assert(std::fabs(step)<1.f);travelled+=step;previous=crossing.angle();
 }
 assert(travelled>1.99f&&travelled<2.01f&&!crossing.active());
 PetOrientation turn;
 float last=0.f;
 for(uint32_t t=0;t<12000;t+=20){
  const float heading=std::fmin(540.f,t*0.06f);
  sample_heading(turn,t,heading,60.f);
  if(t<9000)assert(PetOrientation::delta(turn.angle(),last)>-0.001f);
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
  assert(std::fabs(PetOrientation::delta(busy.angle(),previous))<70.f&&busy.angle()<95.f);
 }
 assert(difference(busy.angle(),90.f)<0.001f&&!busy.active());
 // Angular momentum is state: acceleration is bounded, a reversal initially
 // keeps moving in the old direction, and only then turns back. A free turn
 // overshoots slightly and loses its energy until completely stationary.
 PetOrientation inertia;float max_angle=0.f,previous_velocity=0.f;
 for(uint32_t t=0;t<=3000;t+=20){
  sample_heading(inertia,t,90.f);max_angle=std::fmax(max_angle,inertia.angle());
  assert(std::fabs(inertia.angular_velocity()-previous_velocity)<=36.01f);
  previous_velocity=inertia.angular_velocity();
 }
 assert(max_angle>90.5f&&max_angle<95.f&&!inertia.active()&&inertia.angular_velocity()==0.f);
 PetOrientation reversal;
 for(uint32_t t=0;t<=200;t+=20)sample_heading(reversal,t,90.f);
 assert(reversal.angular_velocity()>100.f);
 for(uint32_t t=220;t<300;t+=20)sample_heading(reversal,t,-90.f);
 const float before_velocity=reversal.angular_velocity();
 sample_heading(reversal,300,-90.f);
 assert(reversal.angular_velocity()>0.f&&reversal.angular_velocity()<before_velocity);
 bool reversed=false;
 for(uint32_t t=320;t<5000;t+=20){
  sample_heading(reversal,t,-90.f);reversed|=reversal.angular_velocity()<0.f;
 }
 assert(reversed&&!reversal.active()&&difference(reversal.angle(),-90.f)<0.001f);
 // Identical angle errors rotate more slowly under loaded wall contact. When
 // separated, ordinary inertia returns immediately; continued wall contact
 // eventually settles too, with no dry-friction lock at the wrong angle.
 PetOrientation free,wall,released;
 float free_error=0.f,wall_error=0.f;
 for(uint32_t t=0;t<=20000;t+=20){
  for(PetOrientation *state:{&free,&wall,&released})state->sample(t,-1.f,0.f,0.f,0.f);
  free.advance(t);wall.advance(t,1.f);released.advance(t,t<700?1.f:0.f);
  if(t==600){free_error=difference(free.angle(),90.f);wall_error=difference(wall.angle(),90.f);}
  if(t==2200){assert(difference(released.angle(),90.f)<0.12f);assert(difference(wall.angle(),90.f)>0.3f);}
 }
 assert(wall_error>free_error+6.f);
 assert(!wall.active()&&wall.angular_velocity()==0.f&&difference(wall.angle(),90.f)==0.f);
 assert(!released.active());
 // Integration tracks elapsed time even when a heavy frame lasts 130-250 ms.
 // With the same accepted target, frequent and busy-frame trajectories agree.
 PetOrientation frequent,slow;
 for(PetOrientation *state:{&frequent,&slow}){
  state->sample(0,-1.f,0.f,0.f,0.f);state->sample(100,-1.f,0.f,0.f,0.f);state->advance(100);
 }
 for(uint32_t t=110;t<=1100;t+=10)frequent.advance(t);
 for(uint32_t t=350;t<=1100;t+=250)slow.advance(t);
 assert(difference(frequent.angle(),slow.angle())<0.4f);
 assert(std::fabs(frequent.angular_velocity()-slow.angular_velocity())<1.5f);
 // A shallow but usable projection amplifies the same accelerometer noise
 // into more degrees. The adaptive deadband must prevent perpetual spring
 // motion for static tilts, including while rendering is relatively slow.
 for(const uint32_t cadence:{25u,200u,300u})for(const float projection:{0.31f,0.4f,0.7f,1.f}){
  for(const float noise:{0.f,0.003f,0.005f})for(unsigned index=0;index<24;++index){
   PetOrientation still;const float heading=index*15.f*0.0174532925199f;
   for(uint32_t t=0;t<30000;t+=cadence){
    const float n=(t/cadence)%2u?noise:-noise;
    still.sample(t,-std::sin(heading)*projection+n,std::cos(heading)*projection+n,
                 std::sqrt(1.f-projection*projection),0.2f);
    still.advance(t);
    if(t>27000u)assert(!still.active());
   }
  }
 }
 // Both sample confirmation and interpolation remain identical on timer wrap.
 PetOrientation ordinary,wrapped;
 for(uint32_t t=0;t<14000;t+=20){
  const float heading=std::sin(t*0.0005f)*160.f;
  sample_heading(ordinary,t,heading,20.f);sample_heading(wrapped,0xfffff000u+t,heading,20.f);
  assert(difference(ordinary.angle(),wrapped.angle())<0.001f);
  assert(ordinary.active()==wrapped.active());
 }
 std::puts("pet orientation: gravity, angular inertia, reversal, bounded overshoot, wall friction, separation, busy frames, 864 noise/tilt cases, faults and wrap passed");
}
