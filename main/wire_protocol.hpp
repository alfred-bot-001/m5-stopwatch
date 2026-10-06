#pragma once
#include <cstdint>
#include <cstring>
namespace wire {
constexpr uint32_t magic=0x56494252;
constexpr uint16_t version=1;
constexpr unsigned audio_samples=480,lease_ms=350;
#pragma pack(push,1)
struct Control {
 uint32_t magic_value=magic;
 uint16_t ver=version,bytes=sizeof(Control);
 uint32_t token=0,session=0;
 uint8_t keys[8]{};
 int8_t wheel=0;
 uint8_t listening=0;
 uint16_t battery_mv=0,level=0,reserved=0;
};
struct Ack {uint32_t magic_value=magic,token=0;uint8_t usb=0,reserved[3]{};};
struct Audio {
 uint32_t magic_value=magic,token=0,session=0,sequence=0;
 int16_t pcm[audio_samples]{};
};
#pragma pack(pop)
static_assert(sizeof(Control)==32 && sizeof(Audio)==976 && sizeof(Ack)==12);
inline bool valid(const Control &p){
 return p.magic_value==magic && p.ver==version && p.bytes==sizeof(p) && p.listening<=1
  && p.reserved==0 && (!p.listening || (p.keys[0]&0x40));
}
// Call only for validated packets in the current control session. Metadata
// changes and idle heartbeats must not keep either display illuminated.
inline bool has_input(const Control &p){
 if(p.listening || p.wheel)return true;
 for(auto key:p.keys)if(key)return true;
 return false;
}
inline bool control_activity(const Control &current,const Control &previous){
 return has_input(current) || has_input(previous); // Include the last release.
}
inline bool fresh(uint32_t now,uint32_t then){return uint32_t(now-then)<lease_ms;}
inline bool newer(uint32_t value,uint32_t previous){return int32_t(value-previous)>0;}
// Bounded FIFO: drift/latency is bounded, stale speech is discarded on release.
struct AudioBuffer {
 int16_t samples[audio_samples*8]{};
 unsigned head=0,count=0;bool playing=false;
 uint32_t sequence=0;bool have_sequence=false;
 unsigned lost=0,overflow=0,underflow=0;
 void clear(){head=count=0;playing=false;have_sequence=false;}
 bool push(const Audio &p){
  if(have_sequence && !newer(p.sequence,sequence))return false;
  if(have_sequence){unsigned gap=p.sequence-sequence-1;lost+=gap;
   if(gap>3){head=count=0;playing=false;}
   else for(unsigned i=0;i<gap*audio_samples;i++)put(0);
  }
  sequence=p.sequence;have_sequence=true;
  for(auto sample:p.pcm)put(sample);
  return true;
 }
 void put(int16_t x){
  if(count==audio_samples*8){head=(head+audio_samples)%(audio_samples*8);count-=audio_samples;++overflow;}
  samples[(head+count)%(audio_samples*8)]=x;++count;
 }
 void read(int16_t *out,unsigned n){
  if(!playing && count>=audio_samples*3)playing=true;
  for(unsigned i=0;i<n;i++){
   if(playing && count){out[i]=samples[head];head=(head+1)%(audio_samples*8);--count;}
   else {out[i]=0;if(playing)++underflow;}
  }
  if(!count)playing=false;
 }
};
}
