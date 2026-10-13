#pragma once
#include <cmath>
#include <cstdint>

// Gravity supplies an up direction only while the screen has enough vertical
// projection. No signed gyro/yaw is available: a flat watch keeps its last
// reliable direction instead of inventing an orientation from sensor noise.
class PetOrientation {
public:
 void sample(uint32_t now,float ax,float ay,float az,float gyro_speed) {
  const uint32_t elapsed=sampled_?uint32_t(now-last_sample_):0u;
  last_sample_=now;sampled_=true;
  const float norm2=ax*ax+ay*ay+az*az,projection2=ax*ax+ay*ay;
  const bool valid=std::isfinite(norm2)&&std::isfinite(projection2)&&std::isfinite(gyro_speed)
   && norm2>=0.78f*0.78f && norm2<=1.22f*1.22f
   && projection2>=0.30f*0.30f && std::fabs(gyro_speed)<=300.f;
  if(!valid){candidate_=false;return;}
  const float heading=wrap(std::atan2(-ax,ay)*57.2957795131f);
  // Abrupt plausible-looking 1 g spikes still need confirmation. Consecutive
  // changes below 18 degrees track a normal hand rotation without restarting
  // this timer on every sample. Allow busy display frames within PetMotion's
  // 300 ms freshness window; older data cannot count as confirmation.
  if(!candidate_ || elapsed>300u || std::fabs(delta(heading,candidate_angle_))>18.f){
   candidate_=true;candidate_at_=now;candidate_angle_=heading;return;
  }
  candidate_angle_=heading;
  if(uint32_t(now-candidate_at_)<80u)return;
  // Holding the accepted target within this small deadband is essential for
  // a static tilted pet to stop repainting in ordinary accelerometer noise.
  if(std::fabs(delta(heading,target_))>1.1f)target_=heading;
 }

 float advance(uint32_t now) {
  if(!started_){started_=true;last_update_=now;return angle_;}
  const float dt=std::fmin(0.10f,uint32_t(now-last_update_)*0.001f);last_update_=now;
  const float difference=delta(target_,angle_);
  angle_=wrap(angle_+difference*(1.f-std::exp(-dt/0.13f)));
  if(std::fabs(delta(target_,angle_))<0.20f)angle_=target_;
  return angle_;
 }

 float angle() const{return angle_;}
 bool active() const{return std::fabs(delta(target_,angle_))>0.001f;}
 static float delta(float to,float from){return wrap(to-from);}
 static float wrap(float angle) {
  float result=std::fmod(angle+180.f,360.f);
  if(result<0.f)result+=360.f;
  return result-180.f;
 }

private:
 bool sampled_=false,started_=false,candidate_=false;
 uint32_t last_sample_=0,last_update_=0,candidate_at_=0;
 float candidate_angle_=0.f,target_=0.f,angle_=0.f;
};
