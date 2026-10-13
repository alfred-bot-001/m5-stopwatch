#include "../main/render_idle.hpp"
#include <cassert>
#include <cstdio>
int main(){
 RenderIdle r;
 assert(r.due(0,1,false,false));r.painted(0,1,false);
 for(unsigned t=1;t<3600000;t+=13)assert(!r.due(t,1,false,false));
 assert(r.due(1,2,false,false));r.painted(1,2,false);
 assert(r.due(2,2,false,true));r.painted(2,2,false);
 assert(!r.due(34,2,true,false));assert(r.due(35,2,true,false));
 r.painted(35,2,true);
 assert(!r.due(67,2,false,false));assert(r.due(68,2,false,false));
 r.painted(68,2,false);assert(!r.due(10000,2,false,false));
 r.painted(0xfffffff0u,2,true);
 assert(!r.due(16,2,false,false));assert(r.due(17,2,false,false));
 puts("render idle: static, state changes, wake, final animation frame and wrap passed");
}
