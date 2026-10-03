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
int main(int argc,char**argv){
 if(argc!=3)return 2;int seconds=atoi(argv[1]);if(seconds<1||seconds>120)return 2;
 AudioObjectPropertyAddress prop={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};UInt32 size=0;
 if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&prop,0,NULL,&size))return 3;
 AudioDeviceID *ids=malloc(size),found=0;AudioObjectGetPropertyData(kAudioObjectSystemObject,&prop,0,NULL,&size,ids);
 for(unsigned i=0;i<size/sizeof(*ids);i++){
  AudioObjectPropertyAddress namep={kAudioObjectPropertyName,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};CFStringRef name;UInt32 n=sizeof(name);char text[256];
  if(!AudioObjectGetPropertyData(ids[i],&namep,0,NULL,&n,&name)){CFStringGetCString(name,text,sizeof(text),kCFStringEncodingUTF8);if(strstr(text,"StopWatch Vibe"))found=ids[i];CFRelease(name);}
 }
 free(ids);if(!found){fprintf(stderr,"USB pad microphone not found\n");return 4;}
 prop=(AudioObjectPropertyAddress){kAudioDevicePropertyStreamFormat,kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMain};AudioStreamBasicDescription fmt;size=sizeof(fmt);
 OSStatus err=AudioObjectGetPropertyData(found,&prop,0,NULL,&size,&fmt);
 if(err||fmt.mFormatID!=kAudioFormatLinearPCM||!(fmt.mFormatFlags&kAudioFormatFlagIsFloat)||fmt.mBitsPerChannel!=32||fmt.mSampleRate!=48000){fprintf(stderr,"Unsupported input format, status %d rate %f bits %u flags %u\n",err,fmt.mSampleRate,(unsigned)fmt.mBitsPerChannel,(unsigned)fmt.mFormatFlags);return 5;}
 limit=seconds*48000;samples=calloc(limit,sizeof(float));AudioDeviceIOProcID proc;
 err=AudioDeviceCreateIOProcID(found,input,NULL,&proc);if(err){fprintf(stderr,"Create input callback: %d\n",err);return 6;}
 err=AudioDeviceStart(found,proc);if(err){fprintf(stderr,"Start input: %d\n",err);return 7;}
 struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
 while(atomic_load(&count)<limit){usleep(10000);clock_gettime(CLOCK_MONOTONIC,&end);if(end.tv_sec-begin.tv_sec>seconds+10)break;}
 clock_gettime(CLOCK_MONOTONIC,&end);AudioDeviceStop(found,proc);AudioDeviceDestroyIOProcID(found,proc);
 unsigned got=atomic_load(&count);FILE *file=fopen(argv[2],"wb");if(!file)return 8;fwrite(samples,sizeof(float),got,file);fclose(file);
 printf("samples=%u expected=%u seconds=%.3f\n",got,limit,end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)/1e9);free(samples);return got==limit?0:9;
}
