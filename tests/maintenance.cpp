#include "../main/maintenance.h"
#include <cassert>
#include <cstdio>
static maintenance_action_t feed(maintenance_t &p,const char *s,uint32_t t){
 maintenance_action_t result=MAINT_NONE;
 for(;*s;s++){auto action=maintenance_byte(&p,*s,t);if(action!=MAINT_NONE){assert(result==MAINT_NONE);result=action;}}
 return result;
}
int main(){
 maintenance_t p={};
 assert(!feed(p,"B\nAT+BOOT\n",0));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",1));
 assert(!feed(p,"VIBE/1 ARM-BOOT 288485439560\n",10));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",2011));
 assert(!feed(p,"VIBE/1 ARM-BOOT 288485439560\n",3000));
 assert(feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",3100)==MAINT_BOOT);
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",3200));
 assert(!feed(p,"VIBE/1 ARM-BOOT 288485439560\n",4000));
 for(int i=0;i<100;i++)assert(!maintenance_byte(&p,'x',4001));
 assert(!feed(p,"\nVIBE/1 CONFIRM-BOOT 288485439560\n",4002));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET 288485439560\n",5000));
 assert(!feed(p,"VIBE/1 ARM-RESET 288485439560\n",5100));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",5200));
 assert(!feed(p,"VIBE/1 ARM-BOOT 288485439560\n",5300));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET 288485439560\n",5400));
 assert(!feed(p,"VIBE/1 ARM-RESET 288485439560\n",5500));
 assert(feed(p,"VIBE/1 CONFIRM-RESET 288485439560\n",5600)==MAINT_POWER_RESET);
 assert(!feed(p,"VIBE/1 CONFIRM-RESET 288485439560\n",5700));
 assert(!feed(p,"VIBE/1 ARM-RESET 288485439560\n",6000));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET 288485439561\n",6100));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET 288485439560\n",6200));
 assert(!feed(p,"VIBE/1 ARM-RESET 288485439560\n",7000));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET 288485439560\n",9001));
 assert(!feed(p,"VIBE/1 ARM-RESET 288485439560\n",0xffffff00));
 assert(feed(p,"VIBE/1 CONFIRM-RESET 288485439560\n",0x100)==MAINT_POWER_RESET);
 puts("PASS legacy bytes ignored, matching confirmation, timeout, one-shot, overflow, wrong device, time wrap");
}
