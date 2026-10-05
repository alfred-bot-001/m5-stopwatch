#include "../main/wire_protocol.hpp"
#include <cassert>
#include <algorithm>
using namespace wire;
int main(){
 Control c;assert(valid(c));c.listening=1;assert(!valid(c));c.keys[0]=0x40;assert(valid(c));c.ver=2;assert(!valid(c));
 assert(fresh(20,0xfffffff0));assert(!fresh(400,1));assert(newer(0,0xffffffff));assert(!newer(4,5));
 AudioBuffer b;Audio p;std::fill(std::begin(p.pcm),std::end(p.pcm),1234);p.sequence=1;
 assert(b.push(p));assert(!b.push(p));int16_t out[480];b.read(out,480);assert(std::all_of(out,out+480,[](auto x){return x==0;}));
 p.sequence=2;assert(b.push(p));p.sequence=3;assert(b.push(p));b.read(out,480);assert(std::all_of(out,out+480,[](auto x){return x==1234;}));
 p.sequence=5;assert(b.push(p));assert(b.lost==1);p.sequence=4;assert(!b.push(p));
 b.clear();b.read(out,480);assert(std::all_of(out,out+480,[](auto x){return x==0;}));
 for(unsigned i=0;i<100;i++){p.sequence=i;b.push(p);}assert(b.count<=3840 && b.overflow>0);
 b.clear();p.sequence=100;b.push(p);p.sequence=200;b.push(p);assert(b.count==480 && !b.playing);
}
