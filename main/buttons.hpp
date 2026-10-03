#pragma once
#include <cstdint>
struct Button {
 bool raw=false, stable=false; uint32_t changed=0;
 void update(bool value,uint32_t now) {
  if(value!=raw){raw=value;changed=now;}
  if(uint32_t(now-changed)>=12)stable=raw;
 }
};
struct Keyboard {
 Button a,b; bool connected=false,armed=false;
 bool update(bool yellow,bool blue,bool active,uint32_t now,uint8_t report[8]) {
  a.update(yellow,now);b.update(blue,now);
  bool edge=active!=connected;
  if(edge){armed=false;connected=active;}
  if(active && !a.raw && !b.raw && !a.stable && !b.stable)armed=true;
  for(int i=0;i<8;i++)report[i]=0;
  if(active && armed){report[0]=a.stable?0x40:0;report[2]=b.stable?0x28:0;}
  return edge;
 }
};
