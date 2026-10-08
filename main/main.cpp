#include <M5Unified.h>
#include <utility/M5IOE1_Class.hpp>
#include "vibe.h"
#include "wireless.h"
#include "buttons.hpp"
#include "gestures.hpp"
#include "diagnostics.hpp"
#include "display_idle.hpp"
#include "character.hpp"
#include "pet_motion.hpp"
#include "power_key.hpp"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/system_reg.h"
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
static std::atomic<unsigned> pmic_sleep{0},pmic_wdt{0},pmic_timer{0},pmic_seconds{0},pmic_source{0},pmic_wake{0},battery_mv{0},usb_mv{0};
static std::atomic<bool> pmic_valid{false},power_reset_requested{false};
static std::atomic<unsigned> pmic_events{0},pmic_event_ms{0},pmic_hold_ms{0},pmic_hold_max_ms{0};
static std::atomic<unsigned> maintenance_intent{0};
static portMUX_TYPE diagnostic_lock=portMUX_INITIALIZER_UNLOCKED;
static RTC_NOINIT_ATTR volatile DiagnosticSnapshot retained_diagnostic;
static DiagnosticSnapshot previous_diagnostic{};
static bool previous_diagnostic_valid=false;
static uint32_t boot_reset_raw=0,boot_strap_raw=0;
static bool led_config_ok=false;
static bool power_key_guard_ok=false;
static bool mic_ok=false;
static bool character_ready=false;
static std::atomic<bool> imu_ready{false};
static std::atomic<unsigned> imu_samples{0},imu_failures{0},pet_hits{0};
static std::atomic<unsigned> imu_read_us{0},pet_render_us{0};
static std::atomic<int> imu_ax{0},imu_ay{0},imu_az{0},pet_x{180},pet_y{155},pet_dizzy{0};
static bool wireless_safe_boot=false;
static TaskHandle_t main_task=nullptr;
static std::atomic<bool> listen_requested{false},capture_enabled{false},mic_running{false};
static std::atomic<unsigned> listen_session{0};
static std::atomic<unsigned> cursor_steps{0},wheel_steps{0},gesture_drops{0};
static constexpr uint8_t screen_brightness=100;
static std::atomic<bool> display_activity{false},display_awake{true};
static std::atomic<uint32_t> display_idle_ms{0};
static QueueHandle_t reports,cursor_events,wheel_events;
static SemaphoreHandle_t screen_lock;
static M5Canvas *screen=nullptr;
static void save_diagnostic(uint32_t now) {
 portENTER_CRITICAL(&diagnostic_lock);
 DiagnosticSnapshot value{0,0,now,
  pmic_buttons.load()|(pmic_config.load()<<8)|(pmic_power.load()<<16)|(pmic_wake.load()<<24),
  unsigned(pmic_valid.load())|(unsigned(connected.load())<<1)|(unsigned(yellow.load())<<2),
  pmic_events.load(),pmic_event_ms.load(),pmic_hold_ms.load(),pmic_hold_max_ms.load(),maintenance_intent.load(),
  boot_reset_raw,boot_strap_raw,vibe_usb_lifecycle(),0};
 diagnostic_store(retained_diagnostic,value);
 portEXIT_CRITICAL(&diagnostic_lock);
}
extern "C" void vibe_note_boot_intent(){maintenance_intent=1;save_diagnostic(esp_timer_get_time()/1000);}
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
 static int previous_transport=0;
 int transport=wireless_ready()?2:(connected.load()?1:0);
 bool active=transport!=0;
 // A route change is a disconnect: release locally and require physical release.
 if(transport!=previous_transport){uint8_t zero[8]{};keyboard.update(keyboard.a.raw,keyboard.b.raw,false,now,zero);xQueueReset(reports);xQueueSend(reports,zero,0);xQueueReset(cursor_events);xQueueReset(wheel_events);cursor.reset();memset(previous,0,8);wireless_keyboard(zero);previous_transport=transport;}

 uint8_t report[8];
 bool old_a=keyboard.a.stable,old_b=keyboard.b.stable;
 bool edge=keyboard.update(!gpio_get_level(GPIO_NUM_2),!gpio_get_level(GPIO_NUM_1),active,now,report);
 if(!old_a && keyboard.a.stable)++yellow_presses;
 if(!old_b && keyboard.b.stable)++blue_presses;
 if(keyboard.a.raw || keyboard.b.raw || old_a!=keyboard.a.stable || old_b!=keyboard.b.stable)display_activity=true;
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
 wireless_state(listen,listen_session.load(),battery_mv.load(),level.load());
 if(!active)return;
 cursor.update(now);
 int8_t direction;
 if(!cursor.busy && xQueueReceive(cursor_events,&direction,0)==pdTRUE)cursor.start(direction,now);
 report[3]=cursor.key;
 if(memcmp(previous,report,8)) {
  if(xQueueSend(reports,report,0)!=pdTRUE){xQueueReset(reports);xQueueSend(reports,report,0);}
  wireless_keyboard(report);
  memcpy(previous,report,8);
 }
}
extern "C" bool vibe_report(uint8_t report[8]){bool got=xQueueReceive(reports,report,0)==pdTRUE;if(got && wireless_ready())memset(report,0,8);return got;}
extern "C" bool vibe_wheel(int8_t *wheel){bool got=xQueueReceive(wheel_events,wheel,0)==pdTRUE;return got && !wireless_ready();}
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
 if(enabled)wireless_audio(pcm,n);
 if(enabled && !M5.Mic.record(pcm,n) && capture_enabled.load() && listen_requested.load())++errors;
}
extern "C" void vibe_pcm(int16_t *out,size_t n) {
 portENTER_CRITICAL(&lock);
 if(wireless_ready() || !capture_enabled.load() || !listen_requested.load()){
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
 snprintf(out,size,"VIBE v=11 reset=%d uptime=%lld board=%d mic=%d usb=%d yellow=%d blue=%d rms=%.6f samples=%u queued=%u drops=%u underflows=%u errors=%u g0=%d presses=%u,%u pm_btn=%02x pm_cfg=%02x pm_pwr=%02x led_cfg_ok=%d pm_id=%08x pm_cfg2=%02x pm_func=%04x pm_drv=%02x pm_io=%02x pm_valid=%d listen=%d gestures=%u,%u gesture_drops=%u pm_sleep=%02x pm_wdt=%u pm_timer=%02x/%u pm_src=%02x pm_wake=%02x mv=%u,%u pm_events=%u pm_evt_ms=%u pm_hold=%u pm_hold_max=%u intent=%u boot_raw=%08x boot_strap=%08x usb_ev=%08x usj_clk=%d prev=%u,%u,%u,%08x,%x,%u,%u,%u,%u,%08x,%08x,%08x\n",
  int(esp_reset_reason()),esp_timer_get_time()/1000,int(M5.getBoard()),int(mic_running.load()),int(connected.load()),int(yellow.load()),int(blue.load()),double(level.load()),c,q,d,u,errors.load(),gpio_get_level(GPIO_NUM_0),yellow_presses.load(),blue_presses.load(),pmic_buttons.load(),pmic_config.load(),pmic_power.load(),led_config_ok,pmic_id.load(),pmic_config2.load(),pmic_func.load(),pmic_drive.load(),pmic_input.load(),int(pmic_valid.load()),int(listen_requested.load()),cursor_steps.load(),wheel_steps.load(),gesture_drops.load(),pmic_sleep.load(),pmic_wdt.load(),pmic_timer.load(),pmic_seconds.load(),pmic_source.load(),pmic_wake.load(),battery_mv.load(),usb_mv.load(),
  pmic_events.load(),pmic_event_ms.load(),pmic_hold_ms.load(),pmic_hold_max_ms.load(),maintenance_intent.load(),unsigned(boot_reset_raw),unsigned(boot_strap_raw),unsigned(vibe_usb_lifecycle()),int(bool(REG_READ(SYSTEM_PERIP_CLK_EN1_REG)&SYSTEM_USB_DEVICE_CLK_EN)),
  unsigned(previous_diagnostic_valid),unsigned(previous_diagnostic.uptime_ms),unsigned(previous_diagnostic.intent),unsigned(previous_diagnostic.pmic_state),unsigned(previous_diagnostic.flags),unsigned(previous_diagnostic.events),unsigned(previous_diagnostic.last_event_ms),unsigned(previous_diagnostic.held_ms),unsigned(previous_diagnostic.max_held_ms),unsigned(previous_diagnostic.reset_raw),unsigned(previous_diagnostic.strap_raw),unsigned(previous_diagnostic.usb_events));
 size_t used=strlen(out);if(used && out[used-1]=='\n')out[--used]=0;
 snprintf(out+used,size-used," key_guard=%d",int(power_key_guard_ok));used=strlen(out);
 char radio[768];wireless_status(radio,sizeof(radio));snprintf(out+used,size-used," main_stack_min=%u radio_safe=%d display_awake=%d display_idle_ms=%u display_timeout_ms=%u character=%d imu=%d imu_samples=%u imu_fail=%u accel=%d,%d,%d pet=%d,%d hits=%u dizzy=%d imu_us=%u render_us=%u %s\n",unsigned(uxTaskGetStackHighWaterMark(main_task)),int(wireless_safe_boot),int(display_awake.load()),unsigned(display_idle_ms.load()),unsigned(kDisplayIdleTimeoutMs),int(character_ready),int(imu_ready.load()),imu_samples.load(),imu_failures.load(),imu_ax.load(),imu_ay.load(),imu_az.load(),pet_x.load(),pet_y.load(),pet_hits.load(),pet_dizzy.load(),imu_read_us.load(),pet_render_us.load(),radio);
}
extern "C" void app_main() {
 main_task=xTaskGetCurrentTaskHandle();
 boot_reset_raw=REG_READ(RTC_CNTL_RESET_STATE_REG);boot_strap_raw=REG_READ(GPIO_STRAP_REG);
 previous_diagnostic=diagnostic_read(retained_diagnostic);
 previous_diagnostic_valid=diagnostic_valid(previous_diagnostic);
 if(!previous_diagnostic_valid)previous_diagnostic={};
 save_diagnostic(esp_timer_get_time()/1000);
 reports=xQueueCreate(32,8);assert(reports);
 cursor_events=xQueueCreate(32,sizeof(int8_t));wheel_events=xQueueCreate(32,sizeof(int8_t));assert(cursor_events && wheel_events);
 auto cfg=M5.config();cfg.internal_spk=false;cfg.internal_imu=false;cfg.internal_rtc=false;
 cfg.fallback_board=m5::board_t::board_M5StopWatch;M5.begin(cfg);
 // Hold both side keys at boot to bypass all wireless initialization. Keyboard
 // arming already requires both keys to be released before any HID is emitted.
 if(!gpio_get_level(GPIO_NUM_1) && !gpio_get_level(GPIO_NUM_2)){
  vTaskDelay(pdMS_TO_TICKS(20));
  wireless_safe_boot=!gpio_get_level(GPIO_NUM_1) && !gpio_get_level(GPIO_NUM_2);
 }
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
 // Apply the reversible guard once at boot, only to the documented register
 // map. Retain double-click power-off and the hardware download escape hatch.
 uint8_t key_before=0,key_after=0;
 if(id[0]==0x50 && id[1]==0x20 && id[2]==0x05 && (id[3]==0x06 || id[3]==0x53)
    && pmic.readRegister(0x49,&key_before,1)){
  const uint8_t guarded=guarded_power_key_config(key_before);
  if((key_before==guarded || pmic.writeRegister8(0x49,guarded))
     && pmic.readRegister(0x49,&key_after,1)){
   pmic_config=key_after;power_key_guard_ok=key_after==guarded;
  }
 }
 if(!power_key_guard_ok)++errors;
 gpio_input_enable(GPIO_NUM_0);
 M5.Display.setBrightness(screen_brightness);M5.Display.fillScreen(TFT_BLACK);
 auto mc=M5.Mic.config();mc.sample_rate=48000;mc.over_sampling=1;mc.magnification=8;mc.task_priority=5;mc.task_pinned_core=1;M5.Mic.config(mc);
 M5.Mic.setBufferReleaseCallback(nullptr,captured);
 mic_ok=M5.getBoard()==m5::board_t::board_M5StopWatch && M5.Mic.isEnabled();
 // The audio rail can survive an ESP-only reset from the old always-on firmware.
 // Begin() restores it on the first press; end() powers down the ADC thereafter.
 if(mic_ok && !M5.getIOExpander(0).digitalWrite(m5::M5IOE1_Class::gpio3,false)){mic_ok=false;++errors;}
 M5Canvas canvas(&M5.Display);canvas.setColorDepth(16);canvas.setPsram(true);
 assert(canvas.createSprite(360,360));
 screen_lock=xSemaphoreCreateMutex();assert(screen_lock);screen=&canvas;
 CharacterRenderer character;character_ready=character.begin();
 PetMotion pet;
 if(!character_ready)++errors;
 // Establish a visible recovery path before starting optional radio work.
 character.draw(canvas,false,false,mic_ok,pet.view());
 canvas.setTextDatum(middle_center);canvas.setTextColor(TFT_DARKGREY,TFT_BLACK);
 canvas.setFont(&lgfx::fonts::Font2);canvas.drawString(wireless_safe_boot?"USB SAFE / WIFI OFF":"USB / STARTING WIFI",180,318);
 canvas.pushSprite((M5.Display.width()-360)/2,(M5.Display.height()-360)/2);
 vibe_usb_init();if(!wireless_safe_boot)wireless_init();
 // BMI270 uploads its configuration at begin(). Keep it after the recovery
 // frame and USB startup, and never retry initialization in the input loop.
 imu_ready=M5.Imu.begin(&M5.In_I2C,M5.getBoard());
 DisplayIdle display_idle(uint32_t(esp_timer_get_time()/1000));
 uint32_t last_power_read=0,last_draw=0,last_imu_read=0;unsigned attempted_session=0;Swipe swipe;PowerButtonHistory power_buttons;
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
   if(M5.Touch.getCount()>0)display_activity=true;
   int x=0,y=0;if(pressed){x=M5.Touch.getDetail().x;y=M5.Touch.getDetail().y;}
   int horizontal=0,vertical=0;swipe.update(connected.load() || wireless_ready(),pressed,x,y,horizontal,vertical);
   for(int i=0;i<abs(horizontal);i++){
    int8_t dir=horizontal>0?1:-1;
    if(xQueueSend(cursor_events,&dir,0)==pdTRUE)++cursor_steps;else ++gesture_drops;
   }
   if(vertical){int8_t step=vertical;wireless_wheel(step);
    if(xQueueSend(wheel_events,&step,0)==pdTRUE)wheel_steps+=abs(vertical);else ++gesture_drops;}
  }
  // Explicit maintenance only: never reset the PMIC automatically at boot.
  // An ESP watchdog reset does not clear the PMIC's own download indicator.
  if(power_reset_requested.exchange(false)){
   maintenance_intent=2;save_diagnostic(esp_timer_get_time()/1000);
   if(!pmic.writeRegister8(0x0c,0xa2)){++errors;maintenance_intent=0;save_diagnostic(esp_timer_get_time()/1000);}
  }
  if(now-last_power_read>=250){last_power_read=now;
   uint8_t buttons[3]={0},gpio[8]={0},system[7]={0},timer[5]={0},voltage[4]={0};
   bool buttons_ok=pmic.readRegister(0x48,buttons,sizeof(buttons));
   power_buttons.sample(buttons_ok,buttons[0],now);
   if(buttons_ok){pmic_buttons=buttons[0];pmic_config=buttons[1];pmic_config2=buttons[2];}
   pmic_events=power_buttons.events;pmic_event_ms=power_buttons.last_event_ms;
   pmic_hold_ms=power_buttons.held_ms;pmic_hold_max_ms=power_buttons.max_held_ms;
   bool ok=buttons_ok && pmic.readRegister(0x10,gpio,sizeof(gpio))
    && pmic.readRegister(0x04,system,sizeof(system)) && pmic.readRegister(0x38,timer,sizeof(timer)) && pmic.readRegister(0x22,voltage,sizeof(voltage));
   if(ok){
    pmic_func=(unsigned(gpio[7])<<8)|gpio[6];pmic_drive=gpio[3];pmic_input=gpio[2];pmic_power=system[2];
    pmic_source=system[0];pmic_wake=system[1];pmic_sleep=system[5];pmic_wdt=system[6];pmic_timer=timer[4];
    pmic_seconds=unsigned(timer[0])|(unsigned(timer[1])<<8)|(unsigned(timer[2])<<16)|(unsigned(timer[3])<<24);
    battery_mv=voltage[0]|(unsigned(voltage[1])<<8);usb_mv=voltage[2]|(unsigned(voltage[3])<<8);}
   pmic_valid=ok;
   save_diagnostic(esp_timer_get_time()/1000);
  }
  bool was_awake=display_idle.awake();
  bool motion_activity=false;
  if(imu_ready.load() && uint32_t(now-last_imu_read)>=(was_awake?25u:200u)){
   last_imu_read=now;
   const int64_t imu_start=esp_timer_get_time();
   const auto updated=M5.Imu.update();
   if(updated & m5::IMU_Class::sensor_mask_accel){
    m5::IMU_Class::imu_data_t data{};M5.Imu.getImuData(&data);
    // The official StopWatch HAL exchanges X/Y. Accelerometer specific force
    // opposes the gravity projection; use negative Y/X for screen right/down.
    const float ax=-data.accel.y,ay=-data.accel.x,az=data.accel.z;
    const float gyro=(updated & m5::IMU_Class::sensor_mask_gyro)
     ?std::sqrt(data.gyro.x*data.gyro.x+data.gyro.y*data.gyro.y+data.gyro.z*data.gyro.z):0.f;
    if(std::isfinite(ax)&&std::isfinite(ay)&&std::isfinite(az)&&std::isfinite(gyro)){
     motion_activity=pet.sample(now,ax,ay,az,gyro);++imu_samples;
     imu_ax=int(std::lround(ax*1000.f));imu_ay=int(std::lround(ay*1000.f));imu_az=int(std::lround(az*1000.f));
    }else ++imu_failures;
   }else ++imu_failures;
   imu_read_us=unsigned(esp_timer_get_time()-imu_start);
  }
  const auto &pet_view=pet.advance(now);
  pet_x=int(std::lround(pet_view.x));pet_y=int(std::lround(pet_view.y));
  pet_hits=pet.hits();pet_dizzy=int(std::lround(pet_view.dizzy*100.f));
  character.update(now,listen_requested.load() && mic_running.load(),level.load());
  bool awake=display_idle.update(now,display_activity.exchange(false) || listen_requested.load() || motion_activity);
  display_idle_ms=display_idle.idle_ms(now);
  if(!awake){
   if(was_awake)M5.Display.setBrightness(0);
   display_awake=false;
   // Keep input, PMIC diagnostics, USB and Wi-Fi running while pixels are dark.
   vTaskDelay(pdMS_TO_TICKS(8));continue;
  }
  bool waking=!was_awake;
  bool held=yellow.load();
  if(!waking && uint32_t(now-last_draw)<((held||pet_view.active)?33u:100u)){vTaskDelay(pdMS_TO_TICKS(8));continue;}
  last_draw=now;
  xSemaphoreTake(screen_lock,portMAX_DELAY);
  const int64_t render_start=esp_timer_get_time();
  character.draw(canvas,held,listen_requested.load() && mic_running.load(),mic_ok,pet_view);
  wireless_view_t radio;wireless_view(&radio);char footer[48];
  if(wireless_safe_boot)snprintf(footer,sizeof(footer),"USB SAFE / WIFI OFF");
  else if(radio.error!=ESP_OK)snprintf(footer,sizeof(footer),"USB / WIFI ERR %X",unsigned(radio.error));
  else snprintf(footer,sizeof(footer),"%s",wireless_ready()?"WIRELESS":(radio.initialized?"USB / SEARCHING":"USB / STARTING WIFI"));
  canvas.setTextDatum(middle_center);canvas.setTextColor(radio.error!=ESP_OK?TFT_ORANGE:(wireless_ready()?TFT_CYAN:TFT_DARKGREY),TFT_BLACK);
  canvas.setFont(&lgfx::fonts::Font2);canvas.drawString(footer,180,318);
  canvas.pushSprite((M5.Display.width()-360)/2,(M5.Display.height()-360)/2);
  pet_render_us=unsigned(esp_timer_get_time()-render_start);
  // Restore the saved level explicitly: setBrightness(0) also changes the
  // library's remembered brightness, so wakeup() alone would stay dark.
  if(waking)M5.Display.setBrightness(screen_brightness);
  display_awake=true;
  xSemaphoreGive(screen_lock);
  vTaskDelay(pdMS_TO_TICKS(8));
 }
}
