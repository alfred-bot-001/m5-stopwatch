// Reads only this prototype's HID reports; does not observe other keyboards.
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
static void report(void *ctx,IOReturn result,void *sender,IOHIDReportType type,uint32_t id,uint8_t *data,CFIndex length){
 (void)ctx;(void)sender;(void)type;
 printf("HID result=%x id=%u data=",result,id);for(CFIndex i=0;i<length;i++)printf("%02x",data[i]);puts("");fflush(stdout);
}
int main(int argc,char **argv){
 IOHIDManagerRef m=IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone);
 int vid=0xcafe,pid=0x4020;CFNumberRef v=CFNumberCreate(NULL,kCFNumberIntType,&vid),p=CFNumberCreate(NULL,kCFNumberIntType,&pid);
 const void *keys[]={CFSTR(kIOHIDVendorIDKey),CFSTR(kIOHIDProductIDKey)};const void *values[]={v,p};
 CFDictionaryRef match=CFDictionaryCreate(NULL,keys,values,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 IOHIDManagerSetDeviceMatching(m,match);IOReturn rc=IOHIDManagerOpen(m,kIOHIDOptionsTypeSeizeDevice);
 printf("HID_OPEN %x\n",rc);fflush(stdout);if(rc)return 1;
 IOHIDManagerRegisterInputReportCallback(m,report,NULL);IOHIDManagerScheduleWithRunLoop(m,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
 CFRunLoopRunInMode(kCFRunLoopDefaultMode,argc>1?atoi(argv[1]):60,false);
 IOHIDManagerUnscheduleFromRunLoop(m,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);IOHIDManagerClose(m,0);CFRelease(m);CFRelease(match);CFRelease(v);CFRelease(p);return 0;
}
