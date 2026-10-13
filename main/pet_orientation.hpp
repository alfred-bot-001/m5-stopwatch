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
  // The same acceleration noise spans more degrees when nearly flat. Scale
  // the deadband with the usable projection so it cannot excite the spring
  // forever, while keeping 1.1-degree sensitivity on a vertical screen.
  const float heading_deadband=std::fmax(1.1f,1.f/std::sqrt(projection2));
  if(std::fabs(delta(heading,target_))>heading_deadband)target_=heading;
 }

 float advance(uint32_t now,float contact=0.f,float sliding_speed=0.f) {
  if(!started_){started_=true;last_update_=now;return angle_;}
  float remaining=std::fmin(0.30f,uint32_t(now-last_update_)*0.001f);last_update_=now;
  while(remaining>0.f){const float dt=std::fmin(0.008f,remaining);remaining-=dt;integrate(dt,contact,sliding_speed);}
  return angle_;
 }

 // Normalized inertia is a visual physical parameter, not a hardware mass.
 // Wall friction resists relative tangential slip at the body's surface. Its
 // bounded torque dissipates slip and adds drag while pressed against a wall;
 // it has no static threshold, so it cannot permanently pin a tilted face.
 void integrate(float dt,float contact=0.f,float sliding_speed=0.f) {
  if(!std::isfinite(dt)||dt<=0.f)return;
  dt=std::fmin(dt,0.008f);
  contact=finite_limit(contact,0.f,1.f);sliding_speed=finite_limit(sliding_speed,-380.f,380.f);
  constexpr float inertia=1.4f,stiffness=70.f,damping=14.f;
  const float rolling_speed=-sliding_speed*(57.2957795f/124.f);
  const float friction=contact*finite_limit((rolling_speed-velocity_)*30.f,-650.f,650.f);
  acceleration_=finite_limit((stiffness*delta(target_,angle_)-damping*velocity_+friction)/inertia,-1800.f,1800.f);
  velocity_=finite_limit(velocity_+acceleration_*dt,-260.f,260.f);
  angle_=wrap(angle_+velocity_*dt);
  if(std::fabs(delta(target_,angle_))<0.12f && std::fabs(velocity_)<0.4f && std::fabs(sliding_speed)<0.5f){
   angle_=target_;velocity_=acceleration_=0.f;
  }
 }

 float angle() const{return angle_;}
 float angular_velocity() const{return velocity_;}
 float angular_acceleration() const{return acceleration_;}
 bool active() const{return std::fabs(delta(target_,angle_))>0.001f || velocity_!=0.f;}
 static float delta(float to,float from){return wrap(to-from);}
 static float wrap(float angle) {
  float result=std::fmod(angle+180.f,360.f);
  if(result<0.f)result+=360.f;
  return result-180.f;
 }

private:
 bool sampled_=false,started_=false,candidate_=false;
 uint32_t last_sample_=0,last_update_=0,candidate_at_=0;
 float candidate_angle_=0.f,target_=0.f,angle_=0.f,velocity_=0.f,acceleration_=0.f;
 static float finite_limit(float value,float lo,float hi){return std::isfinite(value)?std::fmax(lo,std::fmin(hi,value)):0.f;}
};
