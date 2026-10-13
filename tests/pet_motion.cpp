#include "../main/pet_motion.hpp"
#include "../main/display_idle.hpp"
#include <cassert>
#include <limits>
#include <cstdio>

static void bounded(const PetMotionView &v) {
 assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.angle));
 const float dx=v.x-PetMotion::center_x,dy=v.y-PetMotion::center_y;
 assert(dx*dx+dy*dy<=(PetMotion::radius+0.001f)*(PetMotion::radius+0.001f));
 assert(v.angle>=-180.f&&v.angle<180.f);
 assert(std::isfinite(v.upright_angle)&&v.upright_angle>=-180.f&&v.upright_angle<180.f);
 assert(std::isfinite(v.sway)&&std::fabs(v.sway)<=12.001f);
 assert(std::fabs(PetOrientation::delta(v.angle,v.upright_angle+v.sway))<0.001f);
 assert(v.dizzy>=0.f&&v.dizzy<=1.f&&v.squash>=0.f&&v.squash<=1.f);
 assert(std::isfinite(v.angry)&&v.angry>=0.f&&v.angry<=1.f);
}
int main() {
 assert(PetMotion::center_x==233.f&&PetMotion::center_y==233.f&&PetMotion::radius==107.f);
 const PetMotion initial;
 assert(initial.view().x==PetMotion::center_x&&initial.view().y==PetMotion::center_y);
 // Stationary noise cannot wake or extend the 30-minute display timer.
 PetMotion quiet;DisplayIdle idle(0);
 for(uint32_t t=0;t<=1800200;t+=25){
  const float noise=(t%50?1.f:-1.f)*0.005f;
  assert(!quiet.sample(t,noise,-noise,1.f+noise,0.2f));
  bounded(quiet.advance(t));idle.update(t,false);
 }
 assert(!idle.awake()&&quiet.hits()==0&&!quiet.view().active&&quiet.view().angry==0.f);
 // Physical movement wakes an expired display, while its subsequent bounce
 // animation alone cannot keep the display awake.
 const bool movement=quiet.sample(1800225,0.8f,0.f,0.6f,60.f);
 assert(movement&&idle.update(1800225,movement));
 for(uint32_t t=1800250;t<3600450;t+=200){
  const bool activity=quiet.sample(t,0.8f,0.f,0.6f,0.f);
  bounded(quiet.advance(t));assert(!activity);idle.update(t,activity);
 }
 assert(!idle.awake());
 // Fixed tilt slides to a wall, then settles instead of accumulating impacts.
 PetMotion tilt;
 for(uint32_t t=0;t<10000;t+=25){assert(!tilt.sample(t,0.7f,0.5f,0.4f,0.f));bounded(tilt.advance(t));}
 const auto hits=tilt.hits();assert(hits>0);
 for(uint32_t t=10000;t<25000;t+=25){assert(!tilt.sample(t,0.7f,0.5f,0.4f,0.f));bounded(tilt.advance(t));}
 assert(tilt.hits()==hits&&tilt.view().dizzy==0.f&&!tilt.view().active&&tilt.view().angry==0.f);
 assert(std::fabs(PetOrientation::delta(tilt.view().angle,std::atan2(-0.7f,0.5f)*57.2957795131f))<0.2f);
 // A rotated but motionless face is not an animation. Its nonzero resting
 // angle and tiny gravity noise still allow the regular 30-minute sleep.
 PetMotion sideways;DisplayIdle sideways_idle(0);
 for(uint32_t t=0;t<=1800100;t+=25){
  const float noise=t%50u?0.003f:-0.003f;
  const bool active=sideways.sample(t,-1.f+noise,noise,0.f,0.1f);
  bounded(sideways.advance(t));assert(!active);sideways_idle.update(t,active);
 }
 assert(!sideways_idle.awake()&&!sideways.view().active);
 assert(std::fabs(PetOrientation::delta(sideways.view().angle,90.f))<0.3f);
 // A head-on hit rebounds along the normal instead of stopping or reflecting
 // an unrelated axis. A single contact produces one impact, not two axes.
 PetMotion normal_hit;bool bounced=false;
 for(uint32_t t=0;t<5000;t+=8){
  normal_hit.sample(t,0.7f,0.f,0.7f,0.f);bounded(normal_hit.advance(t));
  if(normal_hit.hits()){
   assert(normal_hit.hits()==1);
   const float edge_x=normal_hit.view().x;
   assert(std::fabs(edge_x-PetMotion::center_x-PetMotion::radius)<0.001f);
   normal_hit.sample(t+8,0.7f,0.f,0.7f,0.f);bounded(normal_hit.advance(t+8));
   assert(normal_hit.view().x<edge_x&&std::fabs(normal_hit.view().y-PetMotion::center_y)<0.001f);
   bounced=true;break;
  }
 }
 assert(bounced);
 // Sliding along the rim retains tangential motion. Slowly changing the
 // downhill direction must not manufacture impacts from radial corrections.
 PetMotion rim;
 for(uint32_t t=0;t<15000;t+=25){rim.sample(t,0.7f,0.f,0.7f,0.f);bounded(rim.advance(t));}
 assert(!rim.view().active);
 const auto rim_hits=rim.hits();const float rim_x=rim.view().x,rim_y=rim.view().y;
 for(uint32_t t=15000;t<35000;t+=25){
  const float ay=0.7f*std::fmin(1.f,(t-15000)/8000.f);
  rim.sample(t,0.7f,ay,0.4f,0.f);bounded(rim.advance(t));
  assert(rim.hits()==rim_hits);
 }
 assert(rim.view().y>rim_y+40.f&&rim.view().x<rim_x-15.f);
 assert(!rim.view().active&&rim.view().dizzy==0.f);
 // Every diagonal stays inside the circle, including between the extrema of
 // its bounding box; a rectangular clamp would fail this containment check.
 PetMotion diagonal;
 const float diagonals[4][2]={{4.f,4.f},{-4.f,4.f},{-4.f,-4.f},{4.f,-4.f}};
 for(uint32_t t=0;t<20000;t+=25){
  const auto &a=diagonals[(t/2000)%4];
  diagonal.sample(t,a[0],a[1],0.f,100.f);bounded(diagonal.advance(t));
 }
 assert(diagonal.hits()>2);
 // Strong alternating shake stays in the enclosure and produces dizzy impacts.
 PetMotion shake;unsigned activities=0;float max_dizzy=0.f,max_angry=0.f;
 for(uint32_t t=0;t<6000;t+=25){
  const float a=(t/500)%2?4.f:-4.f;
  activities+=shake.sample(t,a,-a,0.f,160.f);bounded(shake.advance(t));
  max_dizzy=std::fmax(max_dizzy,shake.view().dizzy);
  max_angry=std::fmax(max_angry,shake.view().angry);
 }
 assert(activities>10&&shake.hits()>2&&max_dizzy>0.5f&&max_angry>0.8f);
 // Stopping gradually restores the eyes, with no sticky velocity or old force.
 for(uint32_t t=6000;t<26000;t+=25){shake.sample(t,0.f,0.f,1.f,0.f);bounded(shake.advance(t));}
 assert(shake.view().dizzy==0.f&&!shake.view().active&&shake.view().angry==0.f);
 // A constant extreme tilt, the first sample, or one isolated corrupt sample
 // and its return edge must never be interpreted as sustained strong shaking.
 PetMotion extreme;
 for(uint32_t t=0;t<10000;t+=25){
  extreme.sample(t,4.f,-4.f,0.f,0.f);bounded(extreme.advance(t));assert(extreme.view().angry==0.f);
 }
 PetMotion spike;
 for(uint32_t t=0;t<3000;t+=25){
  const bool corrupt=t==500u;
  spike.sample(t,corrupt?4.f:0.f,corrupt?-4.f:0.f,1.f,corrupt?1500.f:0.f);
  bounded(spike.advance(t));assert(spike.view().angry==0.f);
 }
 // Large angular speed has to persist, while moderate rotation stays calm.
 PetMotion rotation;
 for(uint32_t t=0;t<3000;t+=25){
  rotation.sample(t,0.f,0.f,1.f,100.f);rotation.advance(t);assert(rotation.view().angry==0.f);
 }
 for(uint32_t t=3000;t<3500;t+=25){rotation.sample(t,0.f,0.f,1.f,330.f);bounded(rotation.advance(t));}
 assert(rotation.view().angry>0.8f);
 for(uint32_t t=3500;t<=9000;t+=25){rotation.sample(t,0.f,0.f,1.f,0.f);bounded(rotation.advance(t));}
 assert(rotation.view().angry==0.f);
 for(uint32_t t=9025;t<9500;t+=25){rotation.sample(t,0.f,0.f,1.f,330.f);bounded(rotation.advance(t));}
 assert(rotation.view().angry>0.8f);
 // No samples arriving is not continuing physical interaction. Emotion still
 // cools down after a long renderer pause, without integrating a giant step.
 bounded(rotation.advance(16000));assert(rotation.view().angry==0.f);
 // Sporadic, stale samples cannot accumulate anger across absent sensor time.
 PetMotion sporadic;
 for(uint32_t t=0;t<10000;t+=500){
  sporadic.sample(t,(t/500)%2?4.f:-4.f,0.f,0.f,1000.f);bounded(sporadic.advance(t));
  assert(sporadic.view().angry==0.f);
 }
 // Missing/bad samples stop applying the old force; long gaps cannot teleport.
 PetMotion fault;fault.sample(0,1.f,0.f,0.f,0.f);fault.advance(0);fault.advance(25);
 const float x=fault.view().x;
 assert(!fault.sample(30,std::numeric_limits<float>::quiet_NaN(),0.f,0.f,0.f));
 bounded(fault.advance(60000));assert(std::fabs(fault.view().x-x)<25.f);
 for(uint32_t t=60025;t<75000;t+=25)bounded(fault.advance(t));
 assert(!fault.view().active);
 // Wrapping the uint32 millisecond clock behaves just like ordinary motion.
 PetMotion wrap,normal;
 for(uint32_t t=0;t<20000;t+=25){
  const uint32_t wrapped=0xfffff000u+t;
  const float a=(t/350)%2?1.5f:-1.5f;
  assert(wrap.sample(wrapped,a,-a,0.f,45.f)==normal.sample(t,a,-a,0.f,45.f));
  bounded(wrap.advance(wrapped));normal.advance(t);
  assert(std::fabs(wrap.view().x-normal.view().x)<0.001f);
  assert(std::fabs(wrap.view().dizzy-normal.view().dizzy)<0.001f);
  assert(std::fabs(wrap.view().angry-normal.view().angry)<0.001f);
  assert(std::fabs(PetOrientation::delta(wrap.view().angle,normal.view().angle))<0.001f);
 }
 std::puts("pet motion: circular containment, bounce, settling, dizziness, sustained-shake anger, calm recovery, idle, faults and wrap passed");
}
