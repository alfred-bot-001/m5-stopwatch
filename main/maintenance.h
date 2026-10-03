#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef enum {MAINT_NONE,MAINT_BOOT,MAINT_POWER_RESET} maintenance_action_t;
typedef struct {char line[64];size_t length;bool discard;maintenance_action_t armed;uint32_t last,armed_at;} maintenance_t;
static inline maintenance_action_t maintenance_byte(maintenance_t *p,char c,uint32_t now) {
 if((uint32_t)(now-p->last)>2000){p->length=0;p->discard=false;p->armed=MAINT_NONE;}
 p->last=now;
 if(c=='\n'){
  maintenance_action_t action=MAINT_NONE;p->line[p->length]=0;
  if(!p->discard){
   if(!strcmp(p->line,"VIBE/1 ARM-BOOT 288485439560")){p->armed=MAINT_BOOT;p->armed_at=now;}
   else if(!strcmp(p->line,"VIBE/1 ARM-RESET 288485439560")){p->armed=MAINT_POWER_RESET;p->armed_at=now;}
   else {
    if((uint32_t)(now-p->armed_at)<=2000){
     if(p->armed==MAINT_BOOT && !strcmp(p->line,"VIBE/1 CONFIRM-BOOT 288485439560"))action=MAINT_BOOT;
     if(p->armed==MAINT_POWER_RESET && !strcmp(p->line,"VIBE/1 CONFIRM-RESET 288485439560"))action=MAINT_POWER_RESET;
    }
    p->armed=MAINT_NONE;
   }
  }else p->armed=MAINT_NONE;
  p->length=0;p->discard=false;return action;
 }
 if(p->length+1<sizeof(p->line) && !p->discard)p->line[p->length++]=c;
 else p->discard=true;
 return MAINT_NONE;
}
