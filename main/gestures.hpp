#pragma once
#include <cstdint>
#include <cstdlib>
struct Swipe {
 bool tracking=false,blocked=true;int origin_x=0,origin_y=0,anchor=0,axis=0;
 // Screen pixels: one cursor/scroll step per 24 px. Lock the first dominant axis.
 void update(bool connected,bool pressed,int x,int y,int &cursor,int &wheel){
  cursor=wheel=0;
  if(!connected){tracking=false;blocked=true;return;}
  if(!pressed){tracking=false;blocked=false;axis=0;return;}
  if(blocked)return;
  if(!tracking){tracking=true;origin_x=x;origin_y=y;axis=0;return;}
  if(!axis){
   int dx=x-origin_x,dy=y-origin_y;
   if(abs(dx)<24 && abs(dy)<24)return;
   if(abs(dx)*2>abs(dy)*3){axis=1;anchor=origin_x;}
   else if(abs(dy)*2>abs(dx)*3){axis=2;anchor=origin_y;}
   else return;
  }
  int position=axis==1?x:y,steps=(position-anchor)/24;
  if(steps>8)steps=8;
  if(steps< -8)steps=-8;
  anchor+=steps*24;
  if(axis==1)cursor=steps;else wheel=-steps; // up = positive HID wheel
 }
};
struct CursorPulse {
 uint8_t key=0;bool busy=false;uint32_t changed=0;
 void start(int direction,uint32_t now){key=direction>0?0x4f:0x50;busy=true;changed=now;}
 void update(uint32_t now){
  if(busy && uint32_t(now-changed)>=16){if(key){key=0;changed=now;}else busy=false;}
 }
 void reset(){key=0;busy=false;}
};
