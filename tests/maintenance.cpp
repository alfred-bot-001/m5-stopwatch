#include "../main/maintenance.h"
#include <cassert>
#include <cstdio>
static bool feed(maintenance_t &p,const char *s,uint32_t t){bool boot=false;for(;*s;s++)boot|=maintenance_byte(&p,*s,t);return boot;}
int main(){
 maintenance_t p={};
 assert(!feed(p,"B\nAT+BOOT\n",0));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",1));
 assert(!feed(p,"VIBE/1 ARM-BOOT 288485439560\n",10));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",2011));
 assert(!feed(p,"VIBE/1 ARM-BOOT 288485439560\n",3000));
 assert(feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",3100));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT 288485439560\n",3200));
 assert(!feed(p,"VIBE/1 ARM-BOOT 288485439560\n",4000));
 for(int i=0;i<100;i++)assert(!maintenance_byte(&p,'x',4001));
 assert(!feed(p,"\nVIBE/1 CONFIRM-BOOT 288485439560\n",4002));
 puts("PASS legacy bytes ignored, confirmation required, timeout, one-shot, overflow");
}
