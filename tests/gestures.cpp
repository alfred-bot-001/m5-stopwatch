#include "../main/gestures.hpp"
#include <cassert>
#include <cstdio>
int main(){
 Swipe s;int h,v;
 s.update(true,false,0,0,h,v);
 s.update(true,true,100,100,h,v);
 s.update(true,true,110,107,h,v);assert(h==0 && v==0);
 s.update(true,true,148,110,h,v);assert(h==2 && v==0);
 s.update(true,true,148,200,h,v);assert(h==0 && v==0); // axis stays horizontal
 s.update(true,true,124,200,h,v);assert(h==-1 && v==0);
 s.update(true,false,0,0,h,v);assert(h==0 && v==0);
 s.update(true,true,100,100,h,v);
 s.update(true,true,102,52,h,v);assert(h==0 && v==2);
 s.update(true,true,102,124,h,v);assert(h==0 && v==-3);
 s.update(false,true,102,124,h,v);assert(h==0 && v==0);
 s.update(true,true,102,200,h,v);assert(h==0 && v==0); // no reconnect replay
 s.update(true,false,0,0,h,v);
 s.update(true,true,100,100,h,v);
 s.update(true,true,150,150,h,v);assert(h==0 && v==0); // ambiguous diagonal
 s.update(true,true,900,150,h,v);assert(h==8 && v==0); // bounded burst
 CursorPulse p;p.start(1,100);assert(p.key==0x4f && p.busy);
 p.update(115);assert(p.key==0x4f);
 p.update(116);assert(p.key==0 && p.busy);
 p.update(132);assert(!p.busy);
 p.start(-1,0xfffffff8);p.update(8);assert(p.key==0 && p.busy);
 p.reset();assert(!p.busy && !p.key);
 puts("PASS swipe dead zone, axis lock, reversal, wheel direction, reconnect, burst bound, key release, timer wrap");
}
