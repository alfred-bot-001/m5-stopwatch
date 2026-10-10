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
 // Commands for a different attached board must never arm this device.
 const char *devices[]={"288485439560","9C139E8AC480","D4059278988C"};
 for(const char *device:devices){
  if(!strcmp(device,VIBE_DEVICE_ID))continue;
  char commands[128];
  snprintf(commands,sizeof(commands),"VIBE/1 ARM-BOOT %s\nVIBE/1 CONFIRM-BOOT %s\n",device,device);
  assert(!feed(p,commands,0));
  assert(!feed(p,"VIBE/1 CONFIRM-BOOT " VIBE_DEVICE_ID "\n",0));
 }

 assert(!feed(p,"B\nAT+BOOT\n",0));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT " VIBE_DEVICE_ID "\n",1));
 assert(!feed(p,"VIBE/1 ARM-BOOT " VIBE_DEVICE_ID "\n",10));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT " VIBE_DEVICE_ID "\n",2011));
 assert(!feed(p,"VIBE/1 ARM-BOOT " VIBE_DEVICE_ID "\n",3000));
 assert(feed(p,"VIBE/1 CONFIRM-BOOT " VIBE_DEVICE_ID "\n",3100)==MAINT_BOOT);
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT " VIBE_DEVICE_ID "\n",3200));
 assert(!feed(p,"VIBE/1 ARM-BOOT " VIBE_DEVICE_ID "\n",4000));
 for(int i=0;i<100;i++)assert(!maintenance_byte(&p,'x',4001));
 assert(!feed(p,"\nVIBE/1 CONFIRM-BOOT " VIBE_DEVICE_ID "\n",4002));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET " VIBE_DEVICE_ID "\n",5000));
 assert(!feed(p,"VIBE/1 ARM-RESET " VIBE_DEVICE_ID "\n",5100));
 assert(!feed(p,"VIBE/1 CONFIRM-BOOT " VIBE_DEVICE_ID "\n",5200));
 assert(!feed(p,"VIBE/1 ARM-BOOT " VIBE_DEVICE_ID "\n",5300));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET " VIBE_DEVICE_ID "\n",5400));
 assert(!feed(p,"VIBE/1 ARM-RESET " VIBE_DEVICE_ID "\n",5500));
 assert(feed(p,"VIBE/1 CONFIRM-RESET " VIBE_DEVICE_ID "\n",5600)==MAINT_POWER_RESET);
 assert(!feed(p,"VIBE/1 CONFIRM-RESET " VIBE_DEVICE_ID "\n",5700));
 assert(!feed(p,"VIBE/1 ARM-RESET " VIBE_DEVICE_ID "\n",6000));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET 288485439561\n",6100));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET " VIBE_DEVICE_ID "\n",6200));
 assert(!feed(p,"VIBE/1 ARM-RESET " VIBE_DEVICE_ID "\n",7000));
 assert(!feed(p,"VIBE/1 CONFIRM-RESET " VIBE_DEVICE_ID "\n",9001));
 assert(!feed(p,"VIBE/1 ARM-RESET " VIBE_DEVICE_ID "\n",0xffffff00));
 assert(feed(p,"VIBE/1 CONFIRM-RESET " VIBE_DEVICE_ID "\n",0x100)==MAINT_POWER_RESET);
 puts("PASS legacy bytes ignored, matching confirmation, timeout, one-shot, overflow, wrong device, time wrap");
}
