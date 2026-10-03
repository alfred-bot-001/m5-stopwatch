// Capture only the named USB pad, using CoreAudio's input callback.
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static float *samples;static unsigned limit;static _Atomic unsigned count;
static OSStatus input(AudioDeviceID d,const AudioTimeStamp *now,const AudioBufferList *in,const AudioTimeStamp *ts,AudioBufferList *out,const AudioTimeStamp *ots,void *ctx){
 (void)d;(void)now;(void)ts;(void)out;(void)ots;(void)ctx;
 unsigned pos=atomic_load(&count);
 for(unsigned b=0;b<in->mNumberBuffers&&pos<limit;b++){
  const AudioBuffer *buf=&in->mBuffers[b];if(!buf->mData||!buf->mNumberChannels)continue;
  unsigned frames=buf->mDataByteSize/(sizeof(float)*buf->mNumberChannels);const float *p=buf->mData;
  for(unsigned f=0;f<frames&&pos<limit;f++)samples[pos++]=p[f*buf->mNumberChannels];
  break;
 }
 atomic_store(&count,pos);return noErr;
}
int main(void){
 AudioObjectPropertyAddress prop={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};UInt32 size=0;
 if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&prop,0,NULL,&size))return 3;
 AudioDeviceID *ids=malloc(size),found=0;AudioObjectGetPropertyData(kAudioObjectSystemObject,&prop,0,NULL,&size,ids);
 for(unsigned i=0;i<size/sizeof(*ids);i++){
  AudioObjectPropertyAddress np={kAudioObjectPropertyName,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};CFStringRef name;UInt32 n=sizeof(name);char text[256];
  if(!AudioObjectGetPropertyData(ids[i],&np,0,NULL,&n,&name)){CFStringGetCString(name,text,sizeof(text),kCFStringEncodingUTF8);if(!strcmp(text,"StopWatch Vibe Microphone"))found=ids[i];CFRelease(name);}
 }
 free(ids);if(!found)return 4;
 prop=(AudioObjectPropertyAddress){kAudioHardwarePropertyDefaultInputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
 AudioDeviceID previous=0,verify=0;size=sizeof(previous);AudioObjectGetPropertyData(kAudioObjectSystemObject,&prop,0,NULL,&size,&previous);
 OSStatus err=AudioObjectSetPropertyData(kAudioObjectSystemObject,&prop,0,NULL,sizeof(found),&found);
 AudioObjectGetPropertyData(kAudioObjectSystemObject,&prop,0,NULL,&size,&verify);
 printf("previous=%u StopWatch=%u current=%u status=%d\n",previous,found,verify,err);return err || verify!=found;
}
