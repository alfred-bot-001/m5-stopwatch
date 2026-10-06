#include "vibe.h"
#include "wireless.h"
#include "display_idle.hpp"
#include <M5GFX.h>
#include <algorithm>
#include <atomic>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "soc/gpio_reg.h"
#include "soc/io_mux_reg.h"
#include "soc/soc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstring>
// Use the board's official esp_lcd transport; M5GFX only renders the sprite.
static constexpr int screen_width=320,screen_height=240,stripe_rows=16;
static esp_lcd_panel_io_handle_t panel_io=nullptr;
static esp_lcd_panel_handle_t panel=nullptr;
static uint8_t *screen_dma=nullptr;
static lgfx::LGFX_Sprite *canvas=nullptr;
static SemaphoreHandle_t screen_lock;
static bool display_ok=false;
static std::atomic<bool> display_awake{false};
static std::atomic<uint32_t> display_idle_ms{0};
static std::atomic<esp_err_t> display_power_error{ESP_OK},touch_error{ESP_ERR_INVALID_STATE};
static std::atomic<unsigned> touch_points{0};
static esp_err_t boot_error=ESP_ERR_INVALID_STATE;
static i2c_master_bus_handle_t screen_i2c_bus=nullptr;
static i2c_master_dev_handle_t touch_io=nullptr;
static TaskHandle_t main_task=nullptr;
static const char *screen_stage="not_started";
static esp_err_t screen_error=ESP_OK,pca_read_error=ESP_OK;
static int pca_output=-1,pca_config=-1;
struct BacklightState { unsigned mux; int level; unsigned latch; };
static BacklightState backlight_before{},backlight_configured{};
static uint32_t pins_before_usb[5]{};
static void screen_pin_state(uint32_t *out){
 out[0]=REG_READ(GPIO_FUNC39_OUT_SEL_CFG_REG);out[1]=REG_READ(GPIO_FUNC40_OUT_SEL_CFG_REG);
 out[2]=REG_READ(GPIO_FUNC41_OUT_SEL_CFG_REG);out[3]=REG_READ(GPIO_FUNC42_OUT_SEL_CFG_REG);
 out[4]=REG_READ(GPIO_ENABLE1_REG);
}
static BacklightState backlight_state(){
 return {(REG_READ(IO_MUX_GPIO42_REG)&MCU_SEL_M)>>MCU_SEL_S,
         gpio_get_level(GPIO_NUM_42),(REG_READ(GPIO_OUT1_REG)>>(42-32))&1u};
}
static bool screen_step(const char *stage,esp_err_t error){
 screen_stage=stage;screen_error=error;return error==ESP_OK;
}
static bool screen_init(){
 backlight_before=backlight_state();
 if(!screen_step("backlight_off",gpio_set_level(GPIO_NUM_42,1)))return false;
 // GPIO42 defaults to MTMS. gpio_set_direction() alone does not select
 // the GPIO IO_MUX function; use gpio_config() and keep input enabled for readback.
 gpio_config_t backlight{};backlight.pin_bit_mask=1ULL<<42;
 backlight.mode=GPIO_MODE_INPUT_OUTPUT;backlight.pull_up_en=GPIO_PULLUP_DISABLE;
 backlight.pull_down_en=GPIO_PULLDOWN_DISABLE;backlight.intr_type=GPIO_INTR_DISABLE;
 if(!screen_step("backlight_config",gpio_config(&backlight)))return false;
 // Match the factory firmware's active-low 25 kHz backlight control.
 ledc_timer_config_t light_timer{};light_timer.speed_mode=LEDC_LOW_SPEED_MODE;
 light_timer.duty_resolution=LEDC_TIMER_10_BIT;light_timer.timer_num=LEDC_TIMER_0;
 light_timer.freq_hz=25000;light_timer.clk_cfg=LEDC_AUTO_CLK;
 if(!screen_step("backlight_timer",ledc_timer_config(&light_timer)))return false;
 ledc_channel_config_t light{};light.gpio_num=42;light.speed_mode=LEDC_LOW_SPEED_MODE;
 light.channel=LEDC_CHANNEL_0;light.intr_type=LEDC_INTR_DISABLE;light.timer_sel=LEDC_TIMER_0;
 light.duty=0;light.flags.output_invert=true;
 if(!screen_step("backlight_pwm",ledc_channel_config(&light)))return false;
 backlight_configured=backlight_state();
 i2c_master_bus_config_t cfg{};cfg.i2c_port=I2C_NUM_0;cfg.sda_io_num=GPIO_NUM_1;cfg.scl_io_num=GPIO_NUM_2;cfg.clk_source=I2C_CLK_SRC_DEFAULT;cfg.glitch_ignore_cnt=7;cfg.flags.enable_internal_pullup=true;
 i2c_master_bus_handle_t bus=nullptr;if(!screen_step("i2c_bus",i2c_new_master_bus(&cfg,&bus)))return false;
 screen_i2c_bus=bus;
 i2c_device_config_t device{};device.dev_addr_length=I2C_ADDR_BIT_LEN_7;device.device_address=0x19;device.scl_speed_hz=100000;
 i2c_master_dev_handle_t io=nullptr;if(!screen_step("pca_device",i2c_master_bus_add_device(bus,&device,&io)))return false;
 // Keep LCD CS high while SPI takes control of its clock/data pins.
 const uint8_t commands[][2]={{1,5},{3,0xf8}}; // speaker off, camera power-down, display CS high
 for(auto &cmd:commands)if(!screen_step("pca_write",i2c_master_transmit(io,cmd,2,100)))return false;
 spi_bus_config_t spi{};spi.sclk_io_num=41;spi.mosi_io_num=40;spi.miso_io_num=-1;
 spi.quadwp_io_num=-1;spi.quadhd_io_num=-1;spi.max_transfer_sz=screen_width*stripe_rows*2;
 if(!screen_step("spi_bus",spi_bus_initialize(SPI3_HOST,&spi,SPI_DMA_CH_AUTO)))return false;
 esp_lcd_panel_io_spi_config_t lcd{};lcd.cs_gpio_num=-1;lcd.dc_gpio_num=39;
 lcd.spi_mode=2;lcd.pclk_hz=80000000;lcd.trans_queue_depth=2;lcd.lcd_cmd_bits=8;lcd.lcd_param_bits=8;
 if(!screen_step("panel_io",esp_lcd_new_panel_io_spi(SPI3_HOST,&lcd,&panel_io)))return false;
 esp_lcd_panel_dev_config_t config{};config.reset_gpio_num=-1;
 config.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;config.bits_per_pixel=16;
 if(!screen_step("panel_create",esp_lcd_new_panel_st7789(panel_io,&config,&panel)))return false;
 // The board has an expander-controlled CS. Prime mode-2 SPI while CS is
 // high, as the factory firmware does, before the panel sees clock edges.
 // This first software-reset command is deliberately sent while deselected.
 if(!screen_step("panel_reset",esp_lcd_panel_reset(panel)))return false;
 const uint8_t select[]={1,4};
 if(!screen_step("panel_select",i2c_master_transmit(io,select,sizeof(select),100)))return false;
 for(uint8_t reg:{uint8_t(1),uint8_t(3)}){
  uint8_t value=0;esp_err_t error=i2c_master_transmit_receive(io,&reg,1,&value,1,100);
  if(error!=ESP_OK){pca_read_error=error;continue;}
  if(reg==1)pca_output=value;else pca_config=value;
 }
 if(!screen_step("panel_init",esp_lcd_panel_init(panel)))return false;
 if(!screen_step("panel_invert",esp_lcd_panel_invert_color(panel,true)))return false;
 if(!screen_step("panel_swap",esp_lcd_panel_swap_xy(panel,true)))return false;
 if(!screen_step("panel_mirror",esp_lcd_panel_mirror(panel,true,false)))return false;
 if(!screen_step("panel_on",esp_lcd_panel_disp_on_off(panel,true)))return false;
 canvas=new lgfx::LGFX_Sprite();canvas->setColorDepth(16);canvas->setPsram(true);
 if(!screen_step("canvas",canvas->createSprite(screen_width,screen_height)?ESP_OK:ESP_ERR_NO_MEM))return false;
 screen_dma=(uint8_t*)heap_caps_malloc(screen_width*stripe_rows*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
 if(!screen_step("dma_buffer",screen_dma?ESP_OK:ESP_ERR_NO_MEM))return false;
 screen_lock=xSemaphoreCreateMutex();if(!screen_step("screen_lock",screen_lock?ESP_OK:ESP_ERR_NO_MEM))return false;
 if(!screen_step("backlight_duty",ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,1023*75/100)))return false;
 if(!screen_step("backlight_on",ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0)))return false;
 display_awake=true;
 return screen_step("ready",ESP_OK); // Software initialization only; panel needs visual verification.
}
static bool screen_backlight(bool awake){
 esp_err_t error=ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,awake?1023*75/100:0);
 if(error==ESP_OK)error=ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
 display_power_error=error;
 if(error==ESP_OK)display_awake=awake;
 return error==ESP_OK;
}
static void wake_inputs_init(){
 // BOOT is GPIO0 with an external pull-up; only read it after normal boot.
 gpio_config_t button{};button.pin_bit_mask=1ULL<<0;button.mode=GPIO_MODE_INPUT;
 button.pull_up_en=GPIO_PULLUP_ENABLE;button.pull_down_en=GPIO_PULLDOWN_DISABLE;
 button.intr_type=GPIO_INTR_DISABLE;boot_error=gpio_config(&button);
 if(!screen_i2c_bus)return;
 // FT6336 shares GPIO1/2 with PCA9557. INT is unconnected and RESET is
 // shared with the ESP/LCD: poll touch status without resetting any device.
 i2c_device_config_t device{};device.dev_addr_length=I2C_ADDR_BIT_LEN_7;
 device.device_address=0x38;device.scl_speed_hz=100000;
 touch_error=i2c_master_bus_add_device(screen_i2c_bus,&device,&touch_io);
}
static bool local_display_activity(uint32_t now){
 static uint32_t last_touch_read=0;
 const bool boot_pressed=boot_error==ESP_OK && !gpio_get_level(GPIO_NUM_0);
 // Read TD_STATUS even with the backlight off. A failed read is never an
 // interaction, and the slower error retry avoids a busy loop on a bad bus.
 const uint32_t period=touch_error.load()==ESP_OK?50:500;
 if(touch_io && uint32_t(now-last_touch_read)>=period){
  last_touch_read=now;const uint8_t reg=0x02;uint8_t points=0;
  const esp_err_t error=i2c_master_transmit_receive(touch_io,&reg,1,&points,1,20);
  touch_error=error;
  touch_points=error==ESP_OK && points<=2?points:0;
 }
 return boot_pressed || touch_points.load()!=0;
}
static bool screen_present(){
 const auto *pixels=static_cast<const uint8_t*>(canvas->getBuffer());
 for(int y=0;y<screen_height;y+=stripe_rows){
  const int rows=std::min(stripe_rows,screen_height-y);
  // Sprite RGB565 bytes already have the big-endian order expected by ST7789.
  memcpy(screen_dma,pixels+y*screen_width*2,rows*screen_width*2);
  if(!screen_step("panel_draw",esp_lcd_panel_draw_bitmap(panel,0,y,screen_width,y+rows,screen_dma)))return false;
  // A parameter transaction with no command drains pending DMA transfers.
  // Do not reuse the stripe buffer until SPI has completed reading it.
  if(!screen_step("panel_drain",esp_lcd_panel_io_tx_param(panel_io,-1,nullptr,0)))return false;
 }
 return screen_step("ready",ESP_OK);
}
extern "C" void vibe_connected(bool value){wireless_usb(value);}
extern "C" void vibe_service(){}
extern "C" bool vibe_report(uint8_t *report){return wireless_report(report);}
extern "C" bool vibe_wheel(int8_t *wheel){return wireless_read_wheel(wheel);}
extern "C" void vibe_pcm(int16_t *out,size_t n){wireless_pcm(out,n);}
extern "C" uint8_t *vibe_snapshot(size_t *size){
 if(!display_ok)return nullptr;
 *size=320*240*2;auto *out=(uint8_t*)malloc(*size);
 if(out){xSemaphoreTake(screen_lock,portMAX_DELAY);memcpy(out,canvas->getBuffer(),*size);xSemaphoreGive(screen_lock);}return out;
}
extern "C" void vibe_note_boot_intent(){}
extern "C" void vibe_request_power_reset(){} // Generic receiver has no M5PM1.
extern "C" void vibe_status(char *out,size_t size){
 char radio[768];wireless_status(radio,sizeof(radio));char usb[384];vibe_usb_audio_status(usb,sizeof(usb));
 auto bl=backlight_state();uint32_t pins[5];screen_pin_state(pins);
 snprintf(out,size,"VIBE RX v=1 uptime=%lld screen=%d display_awake=%d display_idle_ms=%u display_timeout_ms=%u display_err=0x%x touch_points=%u touch_err=0x%x boot_err=0x%x screen_driver=esp_lcd spi_mhz=80 bl_pwm=%d screen_stage=%s screen_err=0x%x pca_out=%d pca_cfg=%d pca_err=0x%x bl_before=%u/%d/%u bl_config=%u/%d/%u bl_now=%u/%d/%u pins_pre=%lx/%lx/%lx/%lx/%lx pins_now=%lx/%lx/%lx/%lx/%lx main_stack_min=%u %s %s\n",
  esp_timer_get_time()/1000,int(display_ok),int(display_awake.load()),unsigned(display_idle_ms.load()),unsigned(kDisplayIdleTimeoutMs),unsigned(display_power_error.load()),touch_points.load(),unsigned(touch_error.load()),unsigned(boot_error),display_awake.load()?75:0,
  screen_stage,unsigned(screen_error),pca_output,pca_config,unsigned(pca_read_error),
  backlight_before.mux,backlight_before.level,backlight_before.latch,backlight_configured.mux,backlight_configured.level,backlight_configured.latch,
  bl.mux,bl.level,bl.latch,
  pins_before_usb[0],pins_before_usb[1],pins_before_usb[2],pins_before_usb[3],pins_before_usb[4],
  pins[0],pins[1],pins[2],pins[3],pins[4],unsigned(uxTaskGetStackHighWaterMark(main_task)),radio,usb);
}
static void draw_screen(){
  if(display_ok){
   wireless_view_t v;wireless_view(&v);xSemaphoreTake(screen_lock,portMAX_DELAY);
   auto &c=*canvas;const uint16_t bg=0x0842,muted=0x8410,accent=v.error!=ESP_OK?0xfba0:(v.connected?(v.listening?0xfd65:0x3ed7):0x8410);
   c.fillSprite(bg);c.setTextColor(0xffff,bg);c.setFont(&lgfx::fonts::Font2);c.drawString("STOPWATCH",18,12);c.setTextColor(muted,bg);c.drawRightString("RX C480",302,12);
   c.fillRoundRect(16,42,288,106,16,0x10a4);
   c.fillCircle(38,67,5,accent);c.setTextColor(accent,0x10a4);c.setFont(&lgfx::fonts::efontCN_24);
   c.drawString(v.error!=ESP_OK?"无线故障":(!v.initialized?"正在启动":(!v.connected?"等待 StopWatch":(v.listening?"正在讲话":"已连接"))),53,53);
   c.setFont(&lgfx::fonts::efontCN_16);c.setTextColor(0xce79,0x10a4);
   char detail[64];
   if(v.error!=ESP_OK)snprintf(detail,sizeof(detail),"WIFI %s / 0x%X",v.stage,unsigned(v.error));
   else if(!v.initialized)snprintf(detail,sizeof(detail),"正在初始化无线: %s",v.stage);
   else snprintf(detail,sizeof(detail),"%s",v.connected?"按住黄色键讲话，松开静音":"请开启 StopWatch");
   c.drawString(detail,30,89);
   c.fillRoundRect(30,122,260,8,4,0x2945);unsigned width=v.listening?std::min(260u,unsigned(v.level)*260/12000):0;
   if(width)c.fillRoundRect(30,122,width,8,4,accent);
   c.setTextColor(0xce79,bg);c.setCursor(18,163);if(v.connected)c.printf("电池 %.2f V",v.battery_mv/1000.0);else c.print("电池 --");
   c.setCursor(180,163);c.print(v.host?"USB 已连接":"USB 等待中");
   c.setTextColor(muted,bg);c.setCursor(18,199);c.printf("音频丢包 %u",unsigned(v.lost));c.setFont(&lgfx::fonts::Font2);c.drawRightString("48 kHz / MONO",302,200);
   display_ok=screen_present();xSemaphoreGive(screen_lock);
  }
}
extern "C" void app_main(){
 main_task=xTaskGetCurrentTaskHandle();
 display_ok=screen_init();
 wake_inputs_init();
 screen_pin_state(pins_before_usb);draw_screen();
 vibe_usb_init();wireless_init();
 uint32_t now=uint32_t(esp_timer_get_time()/1000),last_draw=now,observed_activity=0;
 DisplayIdle idle(now);
 for(;;){
  now=uint32_t(esp_timer_get_time()/1000);
  wireless_view_t view;wireless_view(&view);
  const bool remote_activity=view.activity!=observed_activity || (view.connected && view.listening);
  observed_activity=view.activity;
  const bool awake=idle.update(now,local_display_activity(now) || remote_activity);
  display_idle_ms=idle.idle_ms(now);
  if(display_ok){
   if(!awake){
    if(display_awake.load())screen_backlight(false);
   }else if(!display_awake.load() || uint32_t(now-last_draw)>=100){
    // Refresh before lighting up, so wake shows current radio/USB state.
    draw_screen();last_draw=now;
    if(display_ok && !display_awake.load())screen_backlight(true);
   }
  }
  // Poll local wake inputs while asleep; USB/Wi-Fi workers remain running.
  vTaskDelay(pdMS_TO_TICKS(50));
 }
}
