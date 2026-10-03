#include <M5Unified.h>
#include <utility/M5IOE1_Class.hpp>
#include "vibe.h"
#include "buttons.hpp"
#include "gestures.hpp"
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
static std::atomic<unsigned> pmic_power{0};
static std::atomic<unsigned> pmic_id{0},pmic_config2{0},pmic_func{0},pmic_drive{0},pmic_input{0};
static std::atomic<bool> pmic_valid{false},power_reset_requested{false};
static bool led_config_ok=false;
static bool mic_ok=false;
static std::atomic<bool> listen_requested{false},capture_enabled{false},mic_running{false};
static std::atomic<unsigned> listen_session{0};
static std::atomic<unsigned> cursor_steps{0},wheel_steps{0},gesture_drops{0};
static QueueHandle_t reports,cursor_events,wheel_events;
static SemaphoreHandle_t screen_lock;
static M5Canvas *screen=nullptr;
extern "C" uint8_t *vibe_snapshot(size_t *size) {
 if(!screen || !screen_lock)return nullptr;
 *size=360*360*2;auto *result=static_cast<uint8_t*>(malloc(*size));
 if(result){xSemaphoreTake(screen_lock,portMAX_DELAY);memcpy(result,screen->getBuffer(),*size);xSemaphoreGive(screen_lock);}
 return result;
}
extern "C" void vibe_connected(bool value){connected=value;}
extern "C" void vibe_request_power_reset(){power_reset_requested=true;}
extern "C" void vibe_service() {
 static Keyboard keyboard;
 static CursorPulse cursor;
 static uint8_t previous[8]={0};
 uint32_t now=esp_timer_get_time()/1000;
 bool active=connected.load();
 uint8_t report[8];
 bool old_a=keyboard.a.stable,old_b=keyboard.b.stable;
 bool edge=keyboard.update(!gpio_get_level(GPIO_NUM_2),!gpio_get_level(GPIO_NUM_1),active,now,report);
 if(!old_a && keyboard.a.stable)++yellow_presses;
 if(!old_b && keyboard.b.stable)++blue_presses;
 yellow=keyboard.a.stable;blue=keyboard.b.stable;
 bool listen=active && keyboard.armed && keyboard.a.raw && keyboard.a.stable;
 portENTER_CRITICAL(&lock);
 if(listen && !listen_requested.load())++listen_session;
 listen_requested=listen;
 if(!listen){capture_enabled=false;head=count=0;level=0;}
 portEXIT_CRITICAL(&lock);
 if(edge){xQueueReset(reports);xQueueReset(cursor_events);xQueueReset(wheel_events);cursor.reset();memset(previous,0,8);
  if(active)xQueueSend(reports,previous,0);
 }
 if(!active)return;
 cursor.update(now);
 int8_t direction;
 if(!cursor.busy && xQueueReceive(cursor_events,&direction,0)==pdTRUE)cursor.start(direction,now);
 report[3]=cursor.key;
 if(memcmp(previous,report,8)) {
  if(xQueueSend(reports,report,0)!=pdTRUE){xQueueReset(reports);xQueueSend(reports,report,0);}
  memcpy(previous,report,8);
 }
}
extern "C" bool vibe_report(uint8_t report[8]){return xQueueReceive(reports,report,0)==pdTRUE;}
extern "C" bool vibe_wheel(int8_t *wheel){return xQueueReceive(wheel_events,wheel,0)==pdTRUE;}
static void captured(void*,void* data,size_t n) {
 auto *pcm=static_cast<int16_t*>(data);double sum=0;
 for(size_t i=0;i<n;i++)sum+=double(pcm[i])*pcm[i];
 portENTER_CRITICAL(&lock);
 bool enabled=capture_enabled.load() && listen_requested.load();
 if(enabled){level=std::sqrt(sum/n)/32768.0f;
 for(size_t i=0;i<n;i++){
  if(count==4096){head=(head+1)%4096;--count;++drops;}
  fifo[(head+count)%4096]=pcm[i];++count;
 }
 captures+=n;}
 portEXIT_CRITICAL(&lock);
 if(enabled && !M5.Mic.record(pcm,n) && capture_enabled.load() && listen_requested.load())++errors;
}
extern "C" void vibe_pcm(int16_t *out,size_t n) {
 portENTER_CRITICAL(&lock);
 if(!capture_enabled.load() || !listen_requested.load()){
  memset(out,0,n*sizeof(*out));portEXIT_CRITICAL(&lock);return;
 }
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
 snprintf(out,size,"VIBE v=8 reset=%d uptime=%lld board=%d mic=%d usb=%d yellow=%d blue=%d rms=%.6f samples=%u queued=%u drops=%u underflows=%u errors=%u g0=%d presses=%u,%u pm_btn=%02x pm_cfg=%02x pm_pwr=%02x led_cfg_ok=%d pm_id=%08x pm_cfg2=%02x pm_func=%04x pm_drv=%02x pm_io=%02x pm_valid=%d listen=%d gestures=%u,%u gesture_drops=%u\n",
  int(esp_reset_reason()),esp_timer_get_time()/1000,int(M5.getBoard()),int(mic_running.load()),int(connected.load()),int(yellow.load()),int(blue.load()),double(level.load()),c,q,d,u,errors.load(),gpio_get_level(GPIO_NUM_0),yellow_presses.load(),blue_presses.load(),pmic_buttons.load(),pmic_config.load(),pmic_power.load(),led_config_ok,pmic_id.load(),pmic_config2.load(),pmic_func.load(),pmic_drive.load(),pmic_input.load(),int(pmic_valid.load()),int(listen_requested.load()),cursor_steps.load(),wheel_steps.load(),gesture_drops.load());
}
extern "C" void app_main() {
 reports=xQueueCreate(32,8);assert(reports);
 cursor_events=xQueueCreate(32,sizeof(int8_t));wheel_events=xQueueCreate(32,sizeof(int8_t));assert(cursor_events && wheel_events);
 auto cfg=M5.config();cfg.internal_spk=false;cfg.internal_imu=false;cfg.internal_rtc=false;
 cfg.fallback_board=m5::board_t::board_M5StopWatch;M5.begin(cfg);
 // Change only the status LED default level; preserve charging and power rails.
 uint8_t power_before=0,power_after=0;
 auto& pmic=M5.Power.M5pm1;
 uint8_t id[4]={0};
 if(pmic.readRegister(0x00,id,sizeof(id)))pmic_id=(unsigned(id[0])<<24)|(unsigned(id[1])<<16)|(unsigned(id[2])<<8)|id[3];
 if(pmic.readRegister(0x06,&power_before,1) && pmic.setLedEnLevel(false)
    && pmic.readRegister(0x06,&power_after,1)){
  led_config_ok=power_after==(power_before & ~0x10);
  pmic_power=power_after;
 }
 gpio_input_enable(GPIO_NUM_0);
 M5.Display.setBrightness(100);M5.Display.fillScreen(TFT_BLACK);
 auto mc=M5.Mic.config();mc.sample_rate=48000;mc.over_sampling=1;mc.magnification=8;mc.task_priority=5;mc.task_pinned_core=1;M5.Mic.config(mc);
 M5.Mic.setBufferReleaseCallback(nullptr,captured);
 mic_ok=M5.getBoard()==m5::board_t::board_M5StopWatch && M5.Mic.isEnabled();
 // The audio rail can survive an ESP-only reset from the old always-on firmware.
 // Begin() restores it on the first press; end() powers down the ADC thereafter.
 if(mic_ok && !M5.getIOExpander(0).digitalWrite(m5::M5IOE1_Class::gpio3,false)){mic_ok=false;++errors;}
 M5Canvas canvas(&M5.Display);canvas.setColorDepth(16);canvas.setPsram(true);
 assert(canvas.createSprite(360,360));
 screen_lock=xSemaphoreCreateMutex();screen=&canvas;
 vibe_usb_init();
 float envelope=0;
 uint32_t last_power_read=0,last_draw=0;unsigned attempted_session=0;Swipe swipe;
 for(;;){
  uint32_t now=esp_timer_get_time()/1000;
  if(mic_running.load() && (!capture_enabled.load() || !listen_requested.load())){
   capture_enabled=false;M5.Mic.end();mic_running=false;
  }
  if(listen_requested.load() && !mic_running.load() && attempted_session!=listen_session.load()){
   attempted_session=listen_session.load();mic_ok=M5.Mic.begin();mic_running=mic_ok;
   if(!mic_ok)++errors;
   portENTER_CRITICAL(&lock);head=count=0;capture_enabled=mic_ok && listen_requested.load();portEXIT_CRITICAL(&lock);
   if(capture_enabled.load()){
    if(!M5.Mic.record(buffers[0],480) || !M5.Mic.record(buffers[1],480)){++errors;capture_enabled=false;}
   }
  }
  if(M5.Touch.isEnabled()){
   M5.Touch.update(now);bool pressed=M5.Touch.getCount()==1 && M5.Touch.getDetail().isPressed();
   int x=0,y=0;if(pressed){x=M5.Touch.getDetail().x;y=M5.Touch.getDetail().y;}
   int horizontal=0,vertical=0;swipe.update(connected.load(),pressed,x,y,horizontal,vertical);
   for(int i=0;i<abs(horizontal);i++){
    int8_t dir=horizontal>0?1:-1;
    if(xQueueSend(cursor_events,&dir,0)==pdTRUE)++cursor_steps;else ++gesture_drops;
   }
   if(vertical){int8_t step=vertical;
    if(xQueueSend(wheel_events,&step,0)==pdTRUE)wheel_steps+=abs(vertical);else ++gesture_drops;}
  }
  // Explicit maintenance only: never reset the PMIC automatically at boot.
  // An ESP watchdog reset does not clear the PMIC's own download indicator.
  if(power_reset_requested.exchange(false)){
   if(!pmic.writeRegister8(0x0c,0xa2))++errors;
  }
  if(now-last_power_read>=250){last_power_read=now;
   uint8_t buttons[3]={0},gpio[8]={0},power=0;
   bool ok=pmic.readRegister(0x48,buttons,sizeof(buttons)) && pmic.readRegister(0x10,gpio,sizeof(gpio)) && pmic.readRegister(0x06,&power,1);
   if(ok){pmic_buttons=buttons[0];pmic_config=buttons[1];pmic_config2=buttons[2];
    pmic_func=(unsigned(gpio[7])<<8)|gpio[6];pmic_drive=gpio[3];pmic_input=gpio[2];pmic_power=power;}
   pmic_valid=ok;
  }
  float signal=std::fmin(1.f,std::fmax(0.f,(level.load()-0.006f)*18.f));
  envelope=std::fmax(signal,envelope*0.84f);
  float t=esp_timer_get_time()/1000000.0f;bool held=yellow.load();
  if(uint32_t(now-last_draw)<(held?33u:100u)){vTaskDelay(pdMS_TO_TICKS(8));continue;}
  last_draw=now;
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
  if(!listen_requested.load())canvas.drawWideLine(118,97,243,278,8,color);
  canvas.pushSprite((M5.Display.width()-360)/2,(M5.Display.height()-360)/2);
  xSemaphoreGive(screen_lock);
  vTaskDelay(pdMS_TO_TICKS(8));
 }
}
