#pragma once
#include <cmath>
#include <cstdint>
#include "pet_layout.h"

struct PetMotionView {
 float x=PET_SCREEN_CENTER,y=PET_SCREEN_CENTER,angle=0.f,squash=0.f,dizzy=0.f,impact=0.f;
 bool active=false;
};

// Screen-space acceleration is expressed in g, gyro speed in degrees/second.
// This class has no device or rendering dependencies; animation never counts as
// user activity. Only fresh physical movement from sample() can wake the screen.
class PetMotion {
public:
 static constexpr float center_x=PET_SCREEN_CENTER,center_y=PET_SCREEN_CENTER;
 static constexpr float radius=PET_SCREEN_RADIUS-PET_BODY_RADIUS;
 static_assert(radius>0.f,"The pet must fit inside the circular screen");

 bool sample(uint32_t now,float ax,float ay,float az,float gyro_speed) {
  if(!std::isfinite(ax)||!std::isfinite(ay)||!std::isfinite(az)||!std::isfinite(gyro_speed))return false;
  ax=limit(ax,-4.f,4.f);ay=limit(ay,-4.f,4.f);az=limit(az,-4.f,4.f);
  const bool first=!sampled_;
  const float dt=first?0.f:std::fmin(0.25f,uint32_t(now-last_sample_)*0.001f);
  bool activity=false;
  if(first){gx_=anchor_x_=ax;gy_=anchor_y_=ay;anchor_z_=az;last_activity_=now;}
  else {
   const float dx=ax-anchor_x_,dy=ay-anchor_y_,dz=az-anchor_z_;
   // Fixed tilt and normal stationary sensor noise must not keep a display on.
   const bool moved=dx*dx+dy*dy+dz*dz>0.12f*0.12f || std::fabs(gyro_speed)>18.f;
   if(moved && uint32_t(now-last_activity_)>=120){
    activity=true;last_activity_=now;anchor_x_=ax;anchor_y_=ay;anchor_z_=az;
   }
   const float blend=1.f-std::exp(-dt/0.28f);
   gx_+=(ax-gx_)*blend;gy_+=(ay-gy_)*blend;
  }
  // Slow tilt makes the pet slide; a quick movement adds an opposing impulse.
  force_x_=limit(deadzone(gx_)*300.f-(ax-gx_)*530.f,-1600.f,1600.f);
  force_y_=limit(deadzone(gy_)*300.f-(ay-gy_)*530.f,-1600.f,1600.f);
  last_sample_=now;sampled_=true;return activity;
 }

 const PetMotionView &advance(uint32_t now) {
  if(!started_){last_update_=now;started_=true;return view_;}
  float remaining=std::fmin(0.064f,uint32_t(now-last_update_)*0.001f);last_update_=now;
  const bool fresh=sampled_ && uint32_t(now-last_sample_)<=300;
  while(remaining>0.f){
   const float dt=std::fmin(0.008f,remaining);remaining-=dt;
   vx_=limit((vx_+(fresh?force_x_:0.f)*dt)*std::exp(-2.1f*dt),-380.f,380.f);
   vy_=limit((vy_+(fresh?force_y_:0.f)*dt)*std::exp(-2.1f*dt),-380.f,380.f);
   view_.x+=vx_*dt;view_.y+=vy_*dt;
   collide(now,dt);
   view_.squash*=std::exp(-dt/0.16f);view_.impact*=std::exp(-dt/0.40f);
   if(uint32_t(now-last_hit_)>700)view_.dizzy=std::fmax(0.f,view_.dizzy-dt*0.13f);
   phase_=std::fmod(phase_+dt*5.f,6.283185307f);
   const float target=limit(vx_*0.055f+std::sin(phase_)*view_.dizzy*6.f,-12.f,12.f);
   view_.angle+=(target-view_.angle)*(1.f-std::exp(-dt/0.13f));
  }
  if(view_.squash<0.003f)view_.squash=0.f;
  if(view_.impact<0.003f)view_.impact=0.f;
  view_.active=std::fabs(vx_)+std::fabs(vy_)>3.f || view_.dizzy>0.f || view_.impact>0.f || std::fabs(view_.angle)>0.1f;
  return view_;
 }

 const PetMotionView &view() const {return view_;}
 uint32_t hits() const {return hits_;}

private:
 PetMotionView view_;
 bool started_=false,sampled_=false;
 uint32_t last_update_=0,last_sample_=0,last_activity_=0,last_hit_=0,hits_=0;
 float vx_=0.f,vy_=0.f,gx_=0.f,gy_=0.f,force_x_=0.f,force_y_=0.f;
 float anchor_x_=0.f,anchor_y_=0.f,anchor_z_=0.f,phase_=0.f;
 static float limit(float value,float low,float high){return std::fmax(low,std::fmin(high,value));}
 static float deadzone(float value){return std::fabs(value)<0.035f?0.f:value-std::copysign(0.035f,value);}
 void collide(uint32_t now,float dt) {
  const float dx=view_.x-center_x,dy=view_.y-center_y;
  const float distance_squared=dx*dx+dy*dy;
  if(distance_squared<=radius*radius)return;
  const float inverse_distance=1.f/std::sqrt(distance_squared);
  const float nx=dx*inverse_distance,ny=dy*inverse_distance;
  view_.x=center_x+nx*radius;view_.y=center_y+ny*radius;
  const float speed=vx_*nx+vy_*ny;
  if(speed<=0.f)return; // Correct position without bouncing an inward velocity.
  const float tangent_x=vx_-speed*nx,tangent_y=vy_-speed*ny;
  // Only the outward normal component rebounds. Friction lets a fixed tilt
  // settle at the bottom of its circular arc without suppressing edge sliding.
  const float tangent_drag=std::exp(-3.f*dt);
  const float normal_speed=speed<35.f?0.f:-speed*0.64f;
  vx_=tangent_x*tangent_drag+normal_speed*nx;
  vy_=tangent_y*tangent_drag+normal_speed*ny;
  // Resting contact and the small correction for tangential travel around a
  // curved wall are not new impacts; do not count them as dizzy collisions.
  if(speed<45.f)return;
  ++hits_;last_hit_=now;
  view_.squash=std::fmax(view_.squash,limit(speed/230.f,0.f,1.f));
  view_.impact=std::fmax(view_.impact,limit(speed/200.f,0.f,1.f));
  view_.dizzy=limit(view_.dizzy+speed/480.f,0.f,1.f);
 }
};
