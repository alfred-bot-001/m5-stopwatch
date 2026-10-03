#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {char line[64];size_t length;bool discard,armed;uint32_t last,armed_at;} maintenance_t;
static inline bool maintenance_byte(maintenance_t *p,char c,uint32_t now) {
 if((uint32_t)(now-p->last)>2000){p->length=0;p->discard=false;p->armed=false;}
 p->last=now;
 if(c=='\n'){
  bool boot=false;p->line[p->length]=0;
  if(!p->discard){
   if(!strcmp(p->line,"VIBE/1 ARM-BOOT 288485439560")){p->armed=true;p->armed_at=now;}
   else {boot=p->armed && (uint32_t)(now-p->armed_at)<=2000 && !strcmp(p->line,"VIBE/1 CONFIRM-BOOT 288485439560");p->armed=false;}
  }else p->armed=false;
  p->length=0;p->discard=false;return boot;
 }
 if(p->length+1<sizeof(p->line) && !p->discard)p->line[p->length++]=c;
 else p->discard=true;
 return false;
}
