#include <M5Unified.h>
#include "vibe.h"
#include "buttons.hpp"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static int16_t fifo[4096], buffers[2][480];
static unsigned head=0, count=0, drops=0, underflows=0, captures=0;
static std::atomic<float> level{0};
static std::atomic<bool> yellow{false}, blue{false}, connected{false};
static std::atomic<unsigned> errors{0};
static std::atomic<unsigned> yellow_presses{0}, blue_presses{0},pmic_buttons{0},pmic_config{0};
static bool mic_ok=false;
static QueueHandle_t reports;
static SemaphoreHandle_t screen_lock;
static M5Canvas *screen=nullptr;
extern "C" uint8_t *vibe_snapshot(size_t *size) {
 if(!screen || !screen_lock)return nullptr;
 *size=360*360*2;auto *result=static_cast<uint8_t*>(malloc(*size));
 if(result){xSemaphoreTake(screen_lock,portMAX_DELAY);memcpy(result,screen->getBuffer(),*size);xSemaphoreGive(screen_lock);}
 return result;
}
extern "C" void vibe_connected(bool value){connected=value;}
extern "C" void vibe_service() {
 static Keyboard keyboard;
 static uint8_t previous[8]={0};
 uint32_t now=esp_timer_get_time()/1000;
 bool active=connected.load();
 uint8_t report[8];
 bool old_a=keyboard.a.stable,old_b=keyboard.b.stable;
 bool edge=keyboard.update(!gpio_get_level(GPIO_NUM_2),!gpio_get_level(GPIO_NUM_1),active,now,report);
 if(!old_a && keyboard.a.stable)++yellow_presses;
 if(!old_b && keyboard.b.stable)++blue_presses;
 yellow=keyboard.a.stable;blue=keyboard.b.stable;
 if(edge){xQueueReset(reports);memset(previous,0,8);
  if(active)xQueueSend(reports,previous,0);
 }
 if(!active)return;
 if(memcmp(previous,report,8)) {
  if(xQueueSend(reports,report,0)!=pdTRUE){xQueueReset(reports);xQueueSend(reports,report,0);}
  memcpy(previous,report,8);
 }
}
extern "C" bool vibe_report(uint8_t report[8]){return xQueueReceive(reports,report,0)==pdTRUE;}
static void captured(void*,void* data,size_t n) {
 auto *pcm=static_cast<int16_t*>(data);double sum=0;
 for(size_t i=0;i<n;i++)sum+=double(pcm[i])*pcm[i];
 level=std::sqrt(sum/n)/32768.0f;
 portENTER_CRITICAL(&lock);
 for(size_t i=0;i<n;i++){
  if(count==4096){head=(head+1)%4096;--count;++drops;}
  fifo[(head+count)%4096]=pcm[i];++count;
 }
 captures+=n;portEXIT_CRITICAL(&lock);
 if(!M5.Mic.record(pcm,n))++errors;
}
extern "C" void vibe_pcm(int16_t *out,size_t n) {
 portENTER_CRITICAL(&lock);
 // Keep a bounded, recent window when the host begins consuming audio.
 if(count>1920){head=(head+count-960)%4096;count=960;}
 for(size_t i=0;i<n;i++){
  if(count){out[i]=fifo[head];head=(head+1)%4096;--count;}
  else{out[i]=0;++underflows;}
 }
 portEXIT_CRITICAL(&lock);
}
extern "C" void vibe_status(char *out,size_t size) {
 unsigned c,d,u,q;portENTER_CRITICAL(&lock);c=captures;d=drops;u=underflows;q=count;portEXIT_CRITICAL(&lock);
 snprintf(out,size,"VIBE v=5 reset=%d uptime=%lld board=%d mic=%d usb=%d yellow=%d blue=%d rms=%.6f samples=%u queued=%u drops=%u underflows=%u errors=%u g0=%d presses=%u,%u pm_btn=%02x pm_cfg=%02x\n",
  int(esp_reset_reason()),esp_timer_get_time()/1000,int(M5.getBoard()),mic_ok,int(connected.load()),int(yellow.load()),int(blue.load()),double(level.load()),c,q,d,u,errors.load(),gpio_get_level(GPIO_NUM_0),yellow_presses.load(),blue_presses.load(),pmic_buttons.load(),pmic_config.load());
}
extern "C" void app_main() {
 reports=xQueueCreate(32,8);assert(reports);
 auto cfg=M5.config();cfg.internal_spk=false;cfg.internal_imu=false;cfg.internal_rtc=false;
 cfg.fallback_board=m5::board_t::board_M5StopWatch;M5.begin(cfg);
 gpio_input_enable(GPIO_NUM_0);
 M5.Display.setBrightness(100);M5.Display.fillScreen(TFT_BLACK);
 auto mc=M5.Mic.config();mc.sample_rate=48000;mc.over_sampling=1;mc.magnification=8;mc.task_priority=5;mc.task_pinned_core=1;M5.Mic.config(mc);
 M5.Mic.setBufferReleaseCallback(nullptr,captured);
 mic_ok=M5.getBoard()==m5::board_t::board_M5StopWatch && M5.Mic.begin();
 if(mic_ok){M5.Mic.record(buffers[0],480);M5.Mic.record(buffers[1],480);}
 M5Canvas canvas(&M5.Display);canvas.setColorDepth(16);canvas.setPsram(true);
 assert(canvas.createSprite(360,360));
 screen_lock=xSemaphoreCreateMutex();screen=&canvas;
 vibe_usb_init();
 float envelope=0;
 uint32_t last_power_read=0;
 for(;;){
  uint32_t now=esp_timer_get_time()/1000;
  if(now-last_power_read>=250){last_power_read=now;
   pmic_buttons=M5.Power.M5pm1.readRegister8(0x48);pmic_config=M5.Power.M5pm1.readRegister8(0x49);
  }
  float signal=std::fmin(1.f,std::fmax(0.f,(level.load()-0.006f)*18.f));
  envelope=std::fmax(signal,envelope*0.84f);
  float t=esp_timer_get_time()/1000000.0f;bool held=yellow.load();
  float pulse=0.5f+0.5f*sinf(t*(held?9.f:13.f));
  float light=held?0.55f+0.45f*pulse:0.55f+0.45f*envelope*pulse;
  uint16_t color=canvas.color565((held?255:40)*light,(held?180:230)*light,(held?35:200)*light);
  if(!mic_ok)color=TFT_RED;
  xSemaphoreTake(screen_lock,portMAX_DELAY);
  canvas.fillSprite(TFT_BLACK);
  int radius=136+int((held?pulse:envelope*pulse)*15);
  canvas.drawCircle(180,180,radius,color);canvas.drawCircle(180,180,radius-1,color);
  canvas.fillRoundRect(140,80,80,132,40,color);
  canvas.fillRoundRect(116,145,128,100,55,color);
  canvas.fillRoundRect(126,134,108,99,45,TFT_BLACK);
  canvas.fillRoundRect(140,80,80,132,40,color);
  canvas.fillRoundRect(175,236,10,40,5,color);
  canvas.fillRoundRect(147,274,66,10,5,color);
  canvas.pushSprite((M5.Display.width()-360)/2,(M5.Display.height()-360)/2);
  xSemaphoreGive(screen_lock);
  vTaskDelay(pdMS_TO_TICKS(33));
 }
}
