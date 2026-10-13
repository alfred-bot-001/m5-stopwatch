#include <M5Unified.h>
#include <utility/M5IOE1_Class.hpp>
#include "vibe.h"
#include "wireless.h"
#include "buttons.hpp"
#include "gestures.hpp"
#include "diagnostics.hpp"
#include "pmic_diagnostics.hpp"
#include "display_idle.hpp"
#include "character.hpp"
#include "pet_motion.hpp"
#include "power_key.hpp"
#include "pet_layout.h"
#include "battery_level.hpp"
#include "battery_ring.hpp"
#include "power_history.hpp"
#include "power_runtime.hpp"
#include "render_idle.hpp"
#include "esp_private/esp_clk.h"
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
static std::atomic<unsigned> pmic_irq{0},pmic_irq_mask{0};
static std::atomic<bool> pmic_irq_valid{false},pmic_diag_enabled{false};
static std::atomic<unsigned> pmic_checkpoint_request{0},pmic_checkpoint_done{0};
static std::atomic<unsigned> pmic_diag_writes{0},pmic_diag_errors{0},pmic_diag_saved_ms{0};
// Captured before USB starts and never changed afterwards.
static vibe_pmic_diag::Selection previous_pmic_diagnostic;
static bool mic_ok=false;
static bool character_ready=false;
static bool battery_ring_ready=false;
static std::atomic<int> battery_percent{-1};
static std::atomic<unsigned> battery_filtered_mv{0};
static std::atomic<unsigned> pmic_mode{0},pmic_out{0},painted_frames{0};
static std::atomic<bool> pm_ready{false},pm_boost{false};
static std::atomic<bool> imu_ready{false};
static std::atomic<unsigned> imu_samples{0},imu_failures{0},pet_hits{0};
static std::atomic<unsigned> imu_read_us{0},pet_render_us{0};
static std::atomic<int> imu_ax{0},imu_ay{0},imu_az{0},pet_x{233},pet_y{233},pet_dizzy{0},pet_angry{0};
static std::atomic<unsigned> pet_expression{0},pet_mouth{0},pet_fade{100};
static std::atomic<int> pet_angle10{0},pet_upright10{0};
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
static void push_pet_frame(M5Canvas &canvas,const PetMotionView &pet,bool footer_at_top,bool full) {
 static float previous_x=PET_SCREEN_CENTER,previous_y=PET_SCREEN_CENTER;
 static bool previous_footer_at_top=false;
 if(full){M5.Display.clearClipRect();canvas.pushSprite(0,0);}
 else {
  // Erase the last pose and paint the new one without sending all 466² pixels
  // over SPI. The full framebuffer remains available for diagnostic capture.
  constexpr int margin=int(PET_BODY_RADIUS)+3;
  const int left=int(std::floor(std::fmin(previous_x,pet.x)))-margin;
  const int top=int(std::floor(std::fmin(previous_y,pet.y)))-margin;
  const int right=int(std::ceil(std::fmax(previous_x,pet.x)))+margin;
  const int bottom=int(std::ceil(std::fmax(previous_y,pet.y)))+margin;
  // Each push ends its own transaction so the AMOLED driver does not merge
  // the distant pet/footer regions into one nearly full-screen transfer.
  M5.Display.setClipRect(left,top,right-left+1,bottom-top+1);canvas.pushSprite(0,0);
  M5.Display.setClipRect(0,footer_at_top?22:404,PET_SCREEN_SIZE,44);canvas.pushSprite(0,0);
  if(previous_footer_at_top!=footer_at_top){
   M5.Display.setClipRect(0,previous_footer_at_top?22:404,PET_SCREEN_SIZE,44);canvas.pushSprite(0,0);
  }
  M5.Display.clearClipRect();
 }
 previous_x=pet.x;previous_y=pet.y;previous_footer_at_top=footer_at_top;
}
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
extern "C" void vibe_note_boot_intent(){
 maintenance_intent=1;save_diagnostic(esp_timer_get_time()/1000);
 // Main owns PMIC I2C. Give it a bounded opportunity to retain this deliberate
 // download intent before USB tears down; never make recovery depend on I2C.
 const unsigned ticket=pmic_checkpoint_request.fetch_add(1)+1;
 for(unsigned i=0;i<50 && pmic_checkpoint_done.load()!=ticket;++i)vTaskDelay(pdMS_TO_TICKS(10));
}
extern "C" uint8_t *vibe_snapshot(size_t *size) {
 if(!screen || !screen_lock)return nullptr;
 *size=PET_SCREEN_SIZE*PET_SCREEN_SIZE*2;auto *result=static_cast<uint8_t*>(malloc(*size));
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
 vibe_power_history_set_ptt(listen);
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
 VibePowerHistoryStatus hist{};vibe_power_history_status(&hist);
 snprintf(out+used,size-used," key_guard=%d bat_pct=%d bat_mv=%u bat_ring=%d pm_mode=%02x pm_out=%02x pm_ok=%d pm_boost=%d cpu_floor=%d frames=%u hist=%d/%u/%u hist_drop=%u hist_err=%u ctrl_stack=%u",int(power_key_guard_ok),battery_percent.load(),battery_filtered_mv.load(),int(battery_ring_ready),pmic_mode.load(),pmic_out.load(),int(pm_ready.load()),int(pm_boost.load()),pm_ready.load()?80:240,painted_frames.load(),int(hist.ready),unsigned(hist.session),unsigned(hist.records),unsigned(hist.dropped),unsigned(hist.errors),unsigned(vibe_usb_control_stack()));used=strlen(out);
 const auto &previous_pm=previous_pmic_diagnostic.snapshot;
 snprintf(out+used,size-used," pm_irq=%06x pm_mask=%06x pm_irq_ok=%d pm_diag=%d/%u/%u pm_save_ms=%u pm_prev=%d,%u,%u,%02x,%02x,%02x,%02x,%02x,%02x",pmic_irq.load(),pmic_irq_mask.load(),int(pmic_irq_valid.load()),int(pmic_diag_enabled.load()),pmic_diag_writes.load(),pmic_diag_errors.load(),pmic_diag_saved_ms.load(),int(previous_pmic_diagnostic.valid),unsigned(previous_pm.sequence),unsigned(previous_pm.uptime_ms),unsigned(previous_pm.button),unsigned(previous_pm.config),unsigned(previous_pm.wake),unsigned(previous_pm.power),unsigned(previous_pm.flags),unsigned(previous_pm.irq3));used=strlen(out);
 char radio[1024];wireless_status(radio,sizeof(radio));snprintf(out+used,size-used," main_stack_min=%u radio_safe=%d display_awake=%d display_idle_ms=%u display_timeout_ms=%u character=%d imu=%d imu_samples=%u imu_fail=%u accel=%d,%d,%d pet=%d,%d hits=%u dizzy=%d angry=%d face=%u mouth=%u face_fade=%u angle10=%d upright10=%d imu_us=%u render_us=%u %s\n",unsigned(uxTaskGetStackHighWaterMark(main_task)),int(wireless_safe_boot),int(display_awake.load()),unsigned(display_idle_ms.load()),unsigned(kDisplayIdleTimeoutMs),int(character_ready),int(imu_ready.load()),imu_samples.load(),imu_failures.load(),imu_ax.load(),imu_ay.load(),imu_az.load(),pet_x.load(),pet_y.load(),pet_hits.load(),pet_dizzy.load(),pet_angry.load(),pet_expression.load(),pet_mouth.load(),pet_fade.load(),pet_angle10.load(),pet_upright10.load(),imu_read_us.load(),pet_render_us.load(),radio);
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
 const bool known_pmic=id[0]==0x50 && id[1]==0x20 && id[2]==0x05 && (id[3]==0x06 || id[3]==0x53);
 vibe_pmic_diag::Slots pmic_slots;
 uint8_t retained_pm[32]={0};
 // The PMIC's RTC RAM survives loss of the ESP's power domain. Read both slots
 // before changing either; no flash wear and no writes to power/IRQ controls.
 if(known_pmic && pmic.readRegister(vibe_pmic_diag::kBaseRegister,retained_pm,sizeof(retained_pm))){
  for(size_t i=0;i<vibe_pmic_diag::kSlotCount;++i)pmic_slots.load(i,retained_pm+i*vibe_pmic_diag::kSlotSize,vibe_pmic_diag::kSlotSize);
  previous_pmic_diagnostic=pmic_slots.newest();pmic_diag_enabled=true;
 }else ++pmic_diag_errors;
 unsigned pmic_write_failures=0;
 auto checkpoint_pmic=[&](uint32_t stamp){
  if(!pmic_diag_enabled.load() || !pmic_valid.load() || !pmic_irq_valid.load())return;
  vibe_pmic_diag::Snapshot value;
  value.uptime_ms=stamp;value.button=pmic_buttons.load();value.config=pmic_config.load();
  value.wake=pmic_wake.load();value.power=pmic_power.load();value.irq3=pmic_irq.load()>>16;
  value.flags=vibe_pmic_diag::flags(static_cast<vibe_pmic_diag::Intent>(maintenance_intent.load()),gpio_get_level(GPIO_NUM_0),connected.load(),yellow.load(),display_awake.load());
  const auto pending=pmic_slots.prepare(value);vibe_pmic_diag::Slot readback{};
  const uint8_t reg=vibe_pmic_diag::slot_register(pending.index);
  if(pmic.writeRegister(reg,pending.raw.data(),pending.raw.size())
     && pmic.readRegister(reg,readback.data(),readback.size()) && pmic_slots.confirm(pending,readback)){
   ++pmic_diag_writes;pmic_diag_saved_ms=stamp;pmic_write_failures=0;
  }else{
   ++pmic_diag_errors;
   // A failing diagnostic must not become an endless I2C retry workload.
   if(++pmic_write_failures>=3)pmic_diag_enabled=false;
  }
 };
 if(pmic.readRegister(0x06,&power_before,1) && pmic.setLedEnLevel(false)
    && pmic.readRegister(0x06,&power_after,1)){
  led_config_ok=power_after==(power_before & ~0x10);
  pmic_power=power_after;
 }
 // Apply the reversible guard once at boot, only to the documented register
 // map. Retain double-click power-off and the hardware download escape hatch.
 uint8_t key_before=0,key_after=0;
 if(known_pmic && pmic.readRegister(0x49,&key_before,1)){
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
 assert(canvas.createSprite(PET_SCREEN_SIZE,PET_SCREEN_SIZE));
 screen_lock=xSemaphoreCreateMutex();assert(screen_lock);screen=&canvas;
 // Eight cached expression canvases have application lifetime. Keep their
 // control objects off the main task's bounded stack (pixel data uses PSRAM).
 static CharacterRenderer character;character_ready=character.begin();
 BatteryRing battery_ring;battery_ring_ready=battery_ring.begin();
 BatteryLevel battery_level;
 int last_drawn_battery=-1;
 PetMotion pet;
 if(!character_ready)++errors;
 if(!battery_ring_ready)++errors;
 // Establish a visible recovery path before starting optional radio work.
 character.draw(canvas,false,false,mic_ok,pet.view());
 canvas.setTextDatum(middle_center);canvas.setTextColor(TFT_DARKGREY,TFT_BLACK);
 canvas.setFont(&lgfx::fonts::Font2);canvas.drawString(wireless_safe_boot?"USB SAFE / WIFI OFF":"USB / STARTING WIFI",PET_SCREEN_SIZE/2,432);
 battery_ring.draw(canvas,-1); // Unknown until the first valid PMIC reading.
 canvas.pushSprite(0,0);
 vibe_usb_init();if(!wireless_safe_boot)wireless_init();
 // BMI270 uploads its configuration at begin(). Keep it after the recovery
 // frame and USB startup, and never retry initialization in the input loop.
 imu_ready=M5.Imu.begin(&M5.In_I2C,M5.getBoard());
 PowerRuntime power_runtime;pm_ready=power_runtime.begin();
 if(!pm_ready.load())++errors;
 if(!vibe_power_history_begin(unsigned(esp_reset_reason())))++errors;
 RenderIdle render_idle;
 DisplayIdle display_idle(uint32_t(esp_timer_get_time()/1000));
 uint32_t last_power_read=0,last_imu_read=0,last_pmic_checkpoint=0;unsigned attempted_session=0;Swipe swipe;PowerButtonHistory power_buttons;
 for(;;){
  uint32_t now=esp_timer_get_time()/1000;
  const bool interaction_boost=listen_requested.load() || yellow.load() || blue.load() || display_activity.load() || display_idle.idle_ms(now)<2000;
  power_runtime.boost(interaction_boost);pm_boost=power_runtime.held();
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
   checkpoint_pmic(esp_timer_get_time()/1000);
   if(!pmic.writeRegister8(0x0c,0xa2)){++errors;maintenance_intent=0;save_diagnostic(esp_timer_get_time()/1000);}
  }
  if(now-last_power_read>=250){last_power_read=now;
   uint8_t buttons[3]={0},gpio[8]={0},system[7]={0},timer[5]={0},voltage[4]={0},irq[6]={0};
   // IRQ status is latched until explicitly cleared. Observe it without
   // clearing any bit or enabling a new interrupt/wakeup source.
   pmic_irq_valid=pmic.readRegister(0x40,irq,sizeof(irq));
   if(pmic_irq_valid.load()){
    pmic_irq=unsigned(irq[0])|(unsigned(irq[1])<<8)|(unsigned(irq[2])<<16);
    pmic_irq_mask=unsigned(irq[3])|(unsigned(irq[4])<<8)|(unsigned(irq[5])<<16);
   }
   bool buttons_ok=pmic.readRegister(0x48,buttons,sizeof(buttons));
   power_buttons.sample(buttons_ok,buttons[0],now);
   if(buttons_ok){pmic_buttons=buttons[0];pmic_config=buttons[1];pmic_config2=buttons[2];}
   pmic_events=power_buttons.events;pmic_event_ms=power_buttons.last_event_ms;
   pmic_hold_ms=power_buttons.held_ms;pmic_hold_max_ms=power_buttons.max_held_ms;
   bool ok=buttons_ok && pmic.readRegister(0x10,gpio,sizeof(gpio))
    && pmic.readRegister(0x04,system,sizeof(system)) && pmic.readRegister(0x38,timer,sizeof(timer)) && pmic.readRegister(0x22,voltage,sizeof(voltage));
   if(ok){
    pmic_mode=gpio[0];pmic_out=gpio[1];
    pmic_func=(unsigned(gpio[7])<<8)|gpio[6];pmic_drive=gpio[3];pmic_input=gpio[2];pmic_power=system[2];
    pmic_source=system[0];pmic_wake=system[1];pmic_sleep=system[5];pmic_wdt=system[6];pmic_timer=timer[4];
    pmic_seconds=unsigned(timer[0])|(unsigned(timer[1])<<8)|(unsigned(timer[2])<<16)|(unsigned(timer[3])<<24);
    battery_mv=voltage[0]|(unsigned(voltage[1])<<8);usb_mv=voltage[2]|(unsigned(voltage[3])<<8);}
   pmic_valid=ok;
   battery_level.sample(now,ok,battery_mv.load());
   battery_percent=battery_level.percent();battery_filtered_mv=battery_level.filtered_mv();
   VibePowerSample power_sample{};
   power_sample.uptime_ms=uint64_t(esp_timer_get_time()/1000);
   power_sample.valid=ok;power_sample.battery_mv=battery_mv.load();power_sample.usb_mv=usb_mv.load();
   power_sample.percent=battery_percent.load();
   power_sample.charging=ok?(usb_mv.load()>4000 && !(pmic_input.load()&4)):-1;
   power_sample.screen_on=display_awake.load();power_sample.ptt=listen_requested.load();
   power_sample.radio=wireless_ready();power_sample.idle=wireless_power_saving();
   power_sample.cpu_mhz=esp_clk_cpu_freq()/1000000;
   power_sample.pmic_mode=pmic_mode.load();power_sample.pmic_out=pmic_out.load();
   power_sample.pmic_drive=pmic_drive.load();power_sample.pmic_func=pmic_func.load();
   vibe_power_history_sample(&power_sample);
   save_diagnostic(esp_timer_get_time()/1000);
  }
  const unsigned checkpoint_ticket=pmic_checkpoint_request.load();
  if(uint32_t(now-last_pmic_checkpoint)>=1000 || checkpoint_ticket!=pmic_checkpoint_done.load()){
   checkpoint_pmic(esp_timer_get_time()/1000);last_pmic_checkpoint=now;
   // A request arriving during this write must wait for the next checkpoint.
   pmic_checkpoint_done=checkpoint_ticket;
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
  pet_angry=int(std::lround(pet_view.angry*100.f));
  pet_angle10=int(std::lround(pet_view.angle*10.f));
  pet_upright10=int(std::lround(pet_view.upright_angle*10.f));
  const bool real_activity=display_activity.exchange(false) || listen_requested.load() || motion_activity;
  if(real_activity)wireless_activity();
  bool awake=display_idle.update(now,real_activity);
  display_idle_ms=display_idle.idle_ms(now);
  character.update(now,listen_requested.load() && mic_running.load(),level.load(),pet_view,display_idle.idle_ms(now));
  pet_expression=unsigned(character.expression());
  pet_mouth=unsigned(std::lround(character.mouth_open()*100.f));
  pet_fade=character.fade_percent();
  if(!awake){
   if(was_awake)M5.Display.setBrightness(0);
   display_awake=false;
   // Keep input, PMIC diagnostics, USB and Wi-Fi running while pixels are dark.
   vTaskDelay(pdMS_TO_TICKS(20));continue;
  }
  bool waking=!was_awake;
  bool held=yellow.load();
  wireless_view_t radio;wireless_view(&radio);
  const int ring_percent=battery_percent.load();
  const bool animation=held || pet_view.active || character.animating();
  // Quantized resting pose plus visible status: radio heartbeats and unchanged
  // voltage samples cannot turn a static picture into a periodic redraw.
  const uint64_t scene=uint64_t(uint16_t(std::lround(pet_view.x)))
   |(uint64_t(uint16_t(std::lround(pet_view.y)))<<9)
   |(uint64_t(ring_percent+1)<<18)|(uint64_t(held)<<25)
   |(uint64_t(mic_ok)<<26)|(uint64_t(listen_requested.load() && mic_running.load())<<27)
   |(uint64_t(wireless_ready())<<28)|(uint64_t(radio.initialized)<<29)
   |(uint64_t(unsigned(radio.error)&0xffffffffu)<<32);
  if(!render_idle.due(now,scene,animation,waking,pet_expression.load())){
   vTaskDelay(pdMS_TO_TICKS(display_idle.idle_ms(now)>=5000 && !animation?20:8));continue;
  }
  power_runtime.boost(true);
  xSemaphoreTake(screen_lock,portMAX_DELAY);
  const int64_t render_start=esp_timer_get_time();
  character.draw(canvas,held,listen_requested.load() && mic_running.load(),mic_ok,pet_view);
  char footer[48];
  if(wireless_safe_boot)snprintf(footer,sizeof(footer),"USB SAFE / WIFI OFF");
  else if(radio.error!=ESP_OK)snprintf(footer,sizeof(footer),"USB / WIFI ERR %X",unsigned(radio.error));
  else snprintf(footer,sizeof(footer),"%s",wireless_ready()?"WIRELESS":(radio.initialized?"USB / SEARCHING":"USB / STARTING WIFI"));
  canvas.setTextDatum(middle_center);canvas.setTextColor(radio.error!=ESP_OK?TFT_ORANGE:(wireless_ready()?TFT_CYAN:TFT_DARKGREY),TFT_BLACK);
  // Keep connection/PTT feedback opposite the pet when it reaches the lower
  // rim. There is no footer wall: the pet follows the full circular panel.
  const bool footer_at_top=pet_view.y>PET_SCREEN_CENTER+30.f;
  const int footer_y=footer_at_top?34:432,dot_y=footer_at_top?52:414;
  canvas.setFont(&lgfx::fonts::Font2);canvas.drawString(footer,PET_SCREEN_SIZE/2,footer_y);
  const uint16_t dot=!mic_ok?TFT_RED:(held?canvas.color565(255,181,54)
   :(listen_requested.load()&&mic_running.load()?TFT_CYAN:canvas.color565(32,79,76)));
  canvas.drawSpot(PET_SCREEN_CENTER,dot_y,3.f,dot);
  battery_ring.draw(canvas,ring_percent);
  // An updated arc must reach the whole perimeter, including pixels outside
  // the pet/footer dirty rectangles. Ordinary animation keeps partial pushes.
  push_pet_frame(canvas,pet_view,footer_at_top,waking || ring_percent!=last_drawn_battery);
  last_drawn_battery=ring_percent;
  pet_render_us=unsigned(esp_timer_get_time()-render_start);
  // Restore the saved level explicitly: setBrightness(0) also changes the
  // library's remembered brightness, so wakeup() alone would stay dark.
  if(waking)M5.Display.setBrightness(screen_brightness);
  display_awake=true;
  xSemaphoreGive(screen_lock);
  render_idle.painted(now,scene,animation,pet_expression.load());++painted_frames;
  power_runtime.boost(interaction_boost);pm_boost=power_runtime.held();
  vTaskDelay(pdMS_TO_TICKS(8));
 }
}
