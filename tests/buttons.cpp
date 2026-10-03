#include "../main/buttons.hpp"
#include <cassert>
#include <cstdio>
int main(){
 Keyboard k;uint8_t r[8];
 k.update(false,false,true,0,r);
 k.update(true,false,true,10,r);assert(r[0]==0);
 k.update(false,false,true,15,r);k.update(true,false,true,18,r);
 k.update(true,false,true,29,r);assert(r[0]==0);
 k.update(true,false,true,30,r);assert(r[0]==0x40);
 k.update(true,true,true,40,r);k.update(true,true,true,52,r);assert(r[0]==0x40&&r[2]==0x28);
 k.update(false,true,true,60,r);k.update(false,true,true,72,r);assert(r[0]==0&&r[2]==0x28);
 k.update(false,true,false,80,r);assert(r[2]==0);
 k.update(false,true,true,90,r);k.update(false,true,true,110,r);assert(r[2]==0);
 k.update(false,false,true,120,r);k.update(false,false,true,132,r);
 k.update(false,true,true,140,r);k.update(false,true,true,152,r);assert(r[2]==0x28);
 k.update(false,false,true,160,r);k.update(false,false,true,172,r);assert(r[0]==0&&r[2]==0);
 Button wrap;wrap.update(true,0xfffffff8);wrap.update(true,4);assert(wrap.stable);
 puts("PASS debounce, held modifiers, simultaneous keys, release, reconnect suppression, timer wrap");
}
