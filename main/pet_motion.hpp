#pragma once
#include <cmath>
#include <cstdint>
#include "pet_layout.h"
#include "pet_orientation.hpp"

struct PetMotionView {
 float x=PET_SCREEN_CENTER,y=PET_SCREEN_CENTER,angle=0.f,squash=0.f,dizzy=0.f,impact=0.f,angry=0.f;
 // Clockwise screen-space orientation, plus a small relative bounce wobble.
 // The complete angle is wrapped to [-180,180); a nonzero rest angle is idle.
 float upright_angle=0.f,sway=0.f;
 // Signed elastic deformation: +stretch lengthens local Y; +shear tilts the
 // top toward local +X. The renderer bounds its transform independently.
 float stretch=0.f,shear=0.f,contact=0.f,angular_velocity=0.f;
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
  orientation_.sample(now,ax,ay,az,gyro_speed);
  const bool first=!sampled_;
  const uint32_t elapsed=first?0u:uint32_t(now-last_sample_);
  const float dt=std::fmin(0.25f,elapsed*0.001f);
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
  // Anger needs repeated strong physical samples. A one-off bad sample (even
  // followed by a return to normal), fixed tilt, and old data after a long gap
  // cannot build enough evidence. Derivative + minimum delta rejects noise;
  // sustained rapid rotation also qualifies even if acceleration is steady.
  if(!first && elapsed>0u && elapsed<=300u){
   const float dx=ax-previous_x_,dy=ay-previous_y_,dz=az-previous_z_;
   const float delta2=dx*dx+dy*dy+dz*dz;
   const float threshold=std::fmax(0.25f,dt*14.f);
   const bool strong=delta2>threshold*threshold || std::fabs(gyro_speed)>260.f;
   // One delayed sample cannot supply 200 ms of shaking evidence by itself.
   agitation_=limit(agitation_+(strong?std::fmin(dt,0.075f):-dt*0.45f),0.f,0.60f);
   bool repeated_jerks=false;
   if(delta2>1.f){
    if(heavy_jerks_==0 || uint32_t(now-jerk_window_)>1800u){
     jerk_window_=last_jerk_=now;heavy_jerks_=1;
    }else if(uint32_t(now-last_jerk_)>=120u){
     last_jerk_=now;if(heavy_jerks_<3)++heavy_jerks_;
    }
    repeated_jerks=heavy_jerks_>=3;
   }
   if((strong && agitation_>=0.20f) || repeated_jerks){
    last_strong_=now;view_.angry=std::fmax(view_.angry,0.82f);
   }
  }else if(first || elapsed>300u){agitation_=0.f;heavy_jerks_=0;}
  previous_x_=ax;previous_y_=ay;previous_z_=az;
  // Slow tilt makes the pet slide; a quick movement adds an opposing impulse.
  force_x_=limit(deadzone(gx_)*300.f-(ax-gx_)*530.f,-1600.f,1600.f);
  force_y_=limit(deadzone(gy_)*300.f-(ay-gy_)*530.f,-1600.f,1600.f);
  last_sample_=now;sampled_=true;return activity;
 }

 const PetMotionView &advance(uint32_t now) {
  if(!started_){last_update_=now;started_=true;return view_;}
  const uint32_t elapsed=uint32_t(now-last_update_);
  float remaining=std::fmin(0.300f,elapsed*0.001f);last_update_=now;
  // Emotion recovery follows elapsed time even after a paused renderer. The
  // physics integration below still caps long gaps to prevent teleportation.
  const uint32_t since_strong=uint32_t(now-last_strong_);
  const uint32_t calm_ms=since_strong>1000u?since_strong-1000u:0u;
  view_.angry=std::fmax(0.f,view_.angry-std::fmin(float(elapsed),float(calm_ms))*0.00019f);
  const bool fresh=sampled_ && uint32_t(now-last_sample_)<=300;
  while(remaining>0.f){
   const float dt=std::fmin(0.008f,remaining);remaining-=dt;
   // Persistent momentum and normalized mass make the body resist changes of
   // motion. These are animation parameters, not an estimate of real grams.
   constexpr float mass=1.45f,air_drag=1.65f;
   const float old_vx=vx_,old_vy=vy_;
   const float ax=fresh?force_x_/mass:0.f,ay=fresh?force_y_/mass:0.f;
   vx_=limit((vx_+ax*dt)*std::exp(-air_drag*dt),-360.f,360.f);
   vy_=limit((vy_+ay*dt)*std::exp(-air_drag*dt),-360.f,360.f);
   view_.x+=vx_*dt;view_.y+=vy_*dt;
   contact_=sliding_=0.f;collide(now,dt,ax,ay);
   orientation_.integrate(dt,contact_,sliding_);
   view_.upright_angle=orientation_.angle();
   view_.angular_velocity=orientation_.angular_velocity();
   view_.contact=contact_;
   deform(dt,(vx_-old_vx)/dt,(vy_-old_vy)/dt);
   view_.squash*=std::exp(-dt/0.16f);view_.impact*=std::exp(-dt/0.40f);
   if(uint32_t(now-last_hit_)>700)view_.dizzy=std::fmax(0.f,view_.dizzy-dt*0.13f);
   phase_=std::fmod(phase_+dt*5.f,6.283185307f);
   const float target=limit(vx_*0.055f+std::sin(phase_)*view_.dizzy*6.f,-12.f,12.f);
   view_.sway+=(target-view_.sway)*(1.f-std::exp(-dt/0.13f));
  }
  if(view_.squash<0.003f)view_.squash=0.f;
  if(view_.impact<0.003f)view_.impact=0.f;
  if(std::fabs(view_.sway)<0.01f)view_.sway=0.f;
  view_.angle=PetOrientation::wrap(view_.upright_angle+view_.sway);
  view_.active=std::fabs(vx_)+std::fabs(vy_)>3.f || view_.dizzy>0.f || view_.impact>0.f || view_.angry>0.f || std::fabs(view_.sway)>0.1f || orientation_.active() || elastic_active_;
  return view_;
 }

 const PetMotionView &view() const {return view_;}
 uint32_t hits() const {return hits_;}

private:
 PetMotionView view_;
 PetOrientation orientation_;
 bool started_=false,sampled_=false;
 uint32_t last_update_=0,last_sample_=0,last_activity_=0,last_hit_=0,hits_=0,last_strong_=0;
 uint32_t jerk_window_=0,last_jerk_=0;
 uint8_t heavy_jerks_=0;
 float vx_=0.f,vy_=0.f,gx_=0.f,gy_=0.f,force_x_=0.f,force_y_=0.f;
 float anchor_x_=0.f,anchor_y_=0.f,anchor_z_=0.f,phase_=0.f;
 float previous_x_=0.f,previous_y_=0.f,previous_z_=0.f,agitation_=0.f;
 float contact_=0.f,sliding_=0.f,stretch_velocity_=0.f,shear_velocity_=0.f;
 bool elastic_active_=false;
 static float limit(float value,float low,float high){return std::fmax(low,std::fmin(high,value));}
 static float deadzone(float value){return std::fabs(value)<0.035f?0.f:value-std::copysign(0.035f,value);}
 void collide(uint32_t now,float dt,float ax,float ay) {
  const float dx=view_.x-center_x,dy=view_.y-center_y;
  const float distance_squared=dx*dx+dy*dy;
  if(distance_squared<=radius*radius)return;
  const float inverse_distance=1.f/std::sqrt(distance_squared);
  const float nx=dx*inverse_distance,ny=dy*inverse_distance;
  view_.x=center_x+nx*radius;view_.y=center_y+ny*radius;
  const float speed=vx_*nx+vy_*ny;
  if(speed<=0.f)return; // Correct position without bouncing an inward velocity.
  const float tangent_speed=-vx_*ny+vy_*nx;
  const float normal_load=std::fmax(0.f,ax*nx+ay*ny);
  contact_=limit(normal_load/230.f+speed/300.f,0.f,1.f);
  sliding_=tangent_speed;
  // Only the outward normal component rebounds. Friction lets a fixed tilt
  // settle at the bottom of its circular arc without suppressing edge sliding.
  const float slip=tangent_speed+orientation_.angular_velocity()*(0.0174532925f*PET_BODY_RADIUS);
  float tangent_after=tangent_speed-contact_*limit(slip*4.5f,-500.f,500.f)*dt;
  if(std::fabs(tangent_after)<0.5f && std::fabs(-ax*ny+ay*nx)<2.f && std::fabs(orientation_.angular_velocity())<0.4f){
   tangent_after=0.f;sliding_=0.f;
  }
  const float normal_speed=speed<35.f?0.f:-speed*0.56f;
  vx_=-ny*tangent_after+normal_speed*nx;
  vy_=nx*tangent_after+normal_speed*ny;
  // Resting contact and the small correction for tangential travel around a
  // curved wall are not new impacts; do not count them as dizzy collisions.
  if(speed<45.f)return;
  ++hits_;last_hit_=now;
  view_.squash=std::fmax(view_.squash,limit(speed/230.f,0.f,1.f));
  view_.impact=std::fmax(view_.impact,limit(speed/200.f,0.f,1.f));
  view_.dizzy=limit(view_.dizzy+speed/480.f,0.f,1.f);
  const float a=orientation_.angle()*0.0174532925f;
  const float local_nx=std::cos(a)*nx+std::sin(a)*ny;
  const float local_ny=-std::sin(a)*nx+std::cos(a)*ny;
  const float impact=limit(speed/240.f,0.f,1.f);
  stretch_velocity_+=impact*(3.f*std::fabs(local_nx)-8.5f*std::fabs(local_ny));
  shear_velocity_-=impact*local_nx*1.5f;
 }

 void deform(float dt,float ax,float ay) {
  const float a=orientation_.angle()*0.0174532925f,c=std::cos(a),s=std::sin(a);
  const float local_ax=c*ax+s*ay,local_ay=-s*ax+c*ay;
  const float local_vy=-s*vx_+c*vy_;
  float stretch_target=limit(local_vy*0.0013f+local_ay*0.0003f,-0.65f,0.65f);
  float shear_target=limit(-local_ax*0.00025f-orientation_.angular_acceleration()*0.00028f,-0.65f,0.65f);
  if(std::fabs(stretch_target)<0.012f)stretch_target=0.f;
  if(std::fabs(shear_target)<0.012f)shear_target=0.f;
  elastic(view_.stretch,stretch_velocity_,stretch_target,dt);
  elastic(view_.shear,shear_velocity_,shear_target,dt);
  elastic_active_=view_.stretch!=0.f || view_.shear!=0.f || stretch_velocity_!=0.f || shear_velocity_!=0.f;
 }

 static void elastic(float &value,float &velocity,float target,float dt) {
  velocity+=(110.f*(target-value)-14.f*velocity)*dt;
  value+=velocity*dt;
  if(value>1.f){value=1.f;velocity=std::fmin(0.f,velocity);}
  if(value<-1.f){value=-1.f;velocity=std::fmax(0.f,velocity);}
  if(target==0.f && std::fabs(value)<0.002f && std::fabs(velocity)<0.015f){value=velocity=0.f;}
 }
};
