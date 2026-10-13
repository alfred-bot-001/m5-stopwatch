#pragma once
#include <cmath>
#include "pet_deformation_outline.hpp"

namespace pet_deformation {
// The pet's center can reach radius 107. A 120 px geometric silhouette plus
// nearest-neighbour rounding stays inside the battery ring's inner radius 228.
inline constexpr float geometry_radius=120.f;
inline constexpr float singular_limit=1.10f;

inline float finite_unit(float value) {
 return std::isfinite(value)?std::fmax(-1.f,std::fmin(1.f,value)):0.f;
}
inline float finite_angle(float value) {
 return std::isfinite(value)?std::remainder(value,360.f):0.f;
}

struct Transform {
 float xx=1.f,xy=0.f,yx=0.f,yy=1.f;

 Point apply(float x,float y) const {return {xx*x+xy*y,yx*x+yy*y};}
 bool identity(float tolerance=0.001f) const {
  return std::fabs(xx-1.f)<tolerance && std::fabs(xy)<tolerance &&
   std::fabs(yx)<tolerance && std::fabs(yy-1.f)<tolerance;
 }
 float singular_max() const {
  const float aa=xx*xx+yx*yx,bb=xy*xy+yy*yy,ab=xx*xy+yx*yy;
  return std::sqrt(0.5f*(aa+bb+std::sqrt((aa-bb)*(aa-bb)+4.f*ab*ab)));
 }
 float outline_radius() const {
  float largest=0.f;
  for(const auto &point:outline){
   const auto mapped=apply(point.x,point.y);
   largest=std::fmax(largest,mapped.x*mapped.x+mapped.y*mapped.y);
  }
  return std::sqrt(largest);
 }
 // Same pixel-center convention as LGFX::pushRotateZoom. The source pivot
 // maps exactly onto the destination pivot, so every facial feature stays put
 // relative to the same soft body rather than receiving a separate offset.
 void affine(float destination_x,float destination_y,float pivot,float out[6]) const {
  if(!std::isfinite(destination_x))destination_x=233.f;
  if(!std::isfinite(destination_y))destination_y=233.f;
  if(!std::isfinite(pivot))pivot=120.f;
  out[0]=xx;out[1]=xy;out[2]=destination_x+0.5f-(pivot+0.5f)*(xx+xy);
  out[3]=yx;out[4]=yy;out[5]=destination_y+0.5f-(pivot+0.5f)*(yx+yy);
 }
};

inline Transform make(float angle,float stretch,float shear) {
 const float vertical=finite_unit(stretch),lean=finite_unit(shear);
 // Positive stretch lengthens local Y; positive shear drags the head to the
 // right. All elastic channels form ONE matrix: no old impact scale is stacked
 // afterwards, and the stateful springs live in motion, not in this renderer.
 Transform soft{1.f-0.045f*vertical,-0.065f*lean,0.f,1.f+0.12f*vertical};
 const float major=soft.singular_max();
 const float radius=soft.outline_radius();
 const float scale=std::fmin(1.f,std::fmin(singular_limit/major,geometry_radius/radius));
 soft.xx*=scale;soft.xy*=scale;soft.yx*=scale;soft.yy*=scale;
 const float radians=finite_angle(angle)*0.017453292519943295f;
 const float cosine=std::cos(radians),sine=std::sin(radians);
 return {cosine*soft.xx-sine*soft.yx,cosine*soft.xy-sine*soft.yy,
  sine*soft.xx+cosine*soft.yx,sine*soft.xy+cosine*soft.yy};
}
}
