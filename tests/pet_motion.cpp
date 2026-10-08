#include "../main/pet_motion.hpp"
#include "../main/display_idle.hpp"
#include <cassert>
#include <limits>
#include <cstdio>

static void bounded(const PetMotionView &v) {
 assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.angle));
 assert(v.x>=PetMotion::left&&v.x<=PetMotion::right);
 assert(v.y>=PetMotion::top&&v.y<=PetMotion::bottom);
 assert(std::fabs(v.angle)<=12.001f);
 assert(v.dizzy>=0.f&&v.dizzy<=1.f&&v.squash>=0.f&&v.squash<=1.f);
}
int main() {
 // Stationary noise cannot wake or extend the 30-minute display timer.
 PetMotion quiet;DisplayIdle idle(0);
 for(uint32_t t=0;t<=1800200;t+=25){
  const float noise=(t%50?1.f:-1.f)*0.005f;
  assert(!quiet.sample(t,noise,-noise,1.f+noise,0.2f));
  bounded(quiet.advance(t));idle.update(t,false);
 }
 assert(!idle.awake()&&quiet.hits()==0&&!quiet.view().active);
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
 assert(tilt.hits()==hits&&tilt.view().dizzy==0.f&&!tilt.view().active);
 // Strong alternating shake stays in the enclosure and produces dizzy impacts.
 PetMotion shake;unsigned activities=0;float max_dizzy=0.f;
 for(uint32_t t=0;t<6000;t+=25){
  const float a=(t/500)%2?4.f:-4.f;
  activities+=shake.sample(t,a,-a,0.f,160.f);bounded(shake.advance(t));
  max_dizzy=std::fmax(max_dizzy,shake.view().dizzy);
 }
 assert(activities>10&&shake.hits()>2&&max_dizzy>0.5f);
 // Stopping gradually restores the eyes, with no sticky velocity or old force.
 for(uint32_t t=6000;t<26000;t+=25){shake.sample(t,0.f,0.f,1.f,0.f);bounded(shake.advance(t));}
 assert(shake.view().dizzy==0.f&&!shake.view().active);
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
 }
 std::puts("pet motion: bounds, settling, dizziness recovery, idle, faults and clock wrap passed");
}
