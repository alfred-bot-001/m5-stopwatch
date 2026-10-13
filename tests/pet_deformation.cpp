#include "../main/pet_deformation.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

using pet_deformation::make;
static bool close(float a,float b,float epsilon=0.0001f) {return std::fabs(a-b)<=epsilon;}
int main() {
 const auto still=make(0,0,0);
 assert(still.identity());assert(close(still.singular_max(),1));
 assert(still.outline_radius()<120);
 float matrix[6];still.affine(233,200,120,matrix);
 assert(close(matrix[2],113));assert(close(matrix[5],80));

 const auto upright=make(90,0,0);const auto head=upright.apply(0,-85);
 assert(close(head.x,85));assert(close(head.y,0));assert(!upright.identity());
 const auto leaning=make(0,0,1).apply(0,-85);
 assert(leaning.x>5);assert(leaning.y<0);
 const auto tall=make(0,1,0),flat=make(0,-1,0);
 assert(tall.yy/tall.xx>1.16f);assert(flat.yy/flat.xx<0.86f);
 assert(flat.yy<0.89f && flat.xx>1.f);

 // Check the complete transformed convex hull, not each scale in isolation.
 // This includes all supplied silhouettes, Z stroke bounds and raster padding.
 float largest=0,largest_singular=0;
 for(int stretch=-20;stretch<=20;++stretch)for(int shear=-20;shear<=20;++shear){
  for(int angle=-180;angle<=180;angle+=9){
   const auto transform=make(float(angle),stretch/20.f,shear/20.f);
   assert(transform.xx*transform.yy-transform.xy*transform.yx>0.74f);
   const float radius=transform.outline_radius(),major=transform.singular_max();
   assert(radius<=120.0001f);assert(major<=1.1001f);
   largest=std::fmax(largest,radius);largest_singular=std::fmax(largest_singular,major);
   transform.affine(159,307,120,matrix);
   assert(close(matrix[0]*120.5f+matrix[1]*120.5f+matrix[2],159.5f));
   assert(close(matrix[3]*120.5f+matrix[4]*120.5f+matrix[5],307.5f));
   // Sprite deformation and star-head placement share the same transform. A
   // generous 106px original star envelope remains under the body's boundary.
   assert(106.f*major+0.7072f<124.f);
  }
 }
 assert(largest>119.9f);assert(largest_singular>1.04f);

 for(int a=-180;a<180;++a){
  const auto left=make(float(a),.4f,-.7f),same=make(float(a+360),.4f,-.7f);
  assert(close(left.xx,same.xx));assert(close(left.xy,same.xy));
  assert(close(left.yx,same.yx));assert(close(left.yy,same.yy));
 }
 const auto before=make(179.999f,1,1),after=make(-179.999f,1,1);
 assert(close(before.xx,after.xx));assert(close(before.xy,after.xy));
 assert(close(before.yx,after.yx));assert(close(before.yy,after.yy));
 for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
  const auto neutral=make(invalid,invalid,invalid);
  assert(neutral.identity());neutral.affine(invalid,invalid,invalid,matrix);
  for(float value:matrix)assert(std::isfinite(value));
 }
 const auto enormous=make(1e30f,1e30f,-1e30f);
 assert(enormous.outline_radius()<=120.0001f);
 assert(enormous.singular_max()<=1.1001f);
 std::printf("pet deformation: 68921 poses, full hull bound %.5fpx, max singular %.5f, pivot, wrap, finite-input guards passed\n",largest,largest_singular);
}
