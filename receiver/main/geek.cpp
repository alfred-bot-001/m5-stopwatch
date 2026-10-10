#include "vibe.h"
#include "wireless.h"
#include "display_idle.hpp"
#include <M5GFX.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// Waveshare ESP32-S3-GEEK: direct SPI ST7789, with no C480 I/O expander
// or touch controller. GPIO19/20 remain reserved for native USB.
// Pins, panel timings and landscape geometry: Waveshare ESP32-S3-GEEK-Demo,
// MPY/LCD/lcd_example.py. The bundled TFT_eSPI setup also uses 27 MHz SPI.
static constexpr int screen_width=240,screen_height=135,stripe_rows=16;
static constexpr int lcd_sclk=12,lcd_mosi=11,lcd_cs=10,lcd_dc=8,lcd_reset=9,lcd_bl=7;
static constexpr int spi_clock_hz=27000000,backlight_percent=75;
static esp_lcd_panel_io_handle_t panel_io=nullptr;
static esp_lcd_panel_handle_t panel=nullptr;
static lgfx::LGFX_Sprite *canvas=nullptr;
static uint8_t *screen_dma=nullptr;
static SemaphoreHandle_t screen_lock=nullptr,dma_complete=nullptr;
static std::atomic<bool> display_ok{false},display_awake{false};
static std::atomic<uint32_t> display_idle_ms{0},screen_errors{0};
static std::atomic<esp_err_t> screen_error{ESP_OK},display_power_error{ESP_OK};
static std::atomic<const char *> screen_stage{"not_started"};
static esp_err_t boot_error=ESP_ERR_INVALID_STATE,mac_error=ESP_ERR_INVALID_STATE;
static TaskHandle_t main_task=nullptr;
static char short_id[5]="????";

static bool screen_step(const char *stage,esp_err_t error){
 screen_stage=stage;screen_error=error;
 if(error!=ESP_OK)++screen_errors;
 return error==ESP_OK;
}

static bool on_color_done(esp_lcd_panel_io_handle_t,
                          esp_lcd_panel_io_event_data_t *,void *context){
 BaseType_t higher_priority_woken=pdFALSE;
 xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context),&higher_priority_woken);
 return higher_priority_woken==pdTRUE;
}

static bool screen_backlight(bool awake){
 esp_err_t error=ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,
                              awake?1023*backlight_percent/100:0);
 if(error==ESP_OK)error=ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
 display_power_error=error;
 if(error==ESP_OK)display_awake=awake;
 return error==ESP_OK;
}

static bool screen_init(){
 // BL is active high. Configure its GPIO mux explicitly and keep it dark
 // until the first complete status frame has reached the panel.
 if(!screen_step("backlight_off",gpio_set_level(GPIO_NUM_7,0)))return false;
 gpio_config_t backlight{};backlight.pin_bit_mask=1ULL<<lcd_bl;
 backlight.mode=GPIO_MODE_OUTPUT;backlight.pull_up_en=GPIO_PULLUP_DISABLE;
 backlight.pull_down_en=GPIO_PULLDOWN_DISABLE;backlight.intr_type=GPIO_INTR_DISABLE;
 if(!screen_step("backlight_config",gpio_config(&backlight)))return false;
 ledc_timer_config_t timer{};timer.speed_mode=LEDC_LOW_SPEED_MODE;
 timer.duty_resolution=LEDC_TIMER_10_BIT;timer.timer_num=LEDC_TIMER_0;
 timer.freq_hz=1000;timer.clk_cfg=LEDC_AUTO_CLK;
 if(!screen_step("backlight_timer",ledc_timer_config(&timer)))return false;
 ledc_channel_config_t light{};light.gpio_num=lcd_bl;light.speed_mode=LEDC_LOW_SPEED_MODE;
 light.channel=LEDC_CHANNEL_0;light.intr_type=LEDC_INTR_DISABLE;
 light.timer_sel=LEDC_TIMER_0;light.duty=0;
 if(!screen_step("backlight_pwm",ledc_channel_config(&light)))return false;

 screen_lock=xSemaphoreCreateMutex();
 if(!screen_step("screen_lock",screen_lock?ESP_OK:ESP_ERR_NO_MEM))return false;
 dma_complete=xSemaphoreCreateBinary();
 if(!screen_step("dma_semaphore",dma_complete?ESP_OK:ESP_ERR_NO_MEM))return false;
 spi_bus_config_t spi{};spi.sclk_io_num=lcd_sclk;spi.mosi_io_num=lcd_mosi;
 spi.miso_io_num=-1;spi.quadwp_io_num=-1;spi.quadhd_io_num=-1;
 spi.max_transfer_sz=screen_width*stripe_rows*2;
 if(!screen_step("spi_bus",spi_bus_initialize(SPI2_HOST,&spi,SPI_DMA_CH_AUTO)))return false;
 esp_lcd_panel_io_spi_config_t io{};io.cs_gpio_num=lcd_cs;io.dc_gpio_num=lcd_dc;
 io.spi_mode=0;io.pclk_hz=spi_clock_hz;io.trans_queue_depth=2;
 io.lcd_cmd_bits=8;io.lcd_param_bits=8;
 if(!screen_step("panel_io",esp_lcd_new_panel_io_spi(SPI2_HOST,&io,&panel_io)))return false;
 esp_lcd_panel_io_callbacks_t callbacks{};callbacks.on_color_trans_done=on_color_done;
 if(!screen_step("panel_callback",esp_lcd_panel_io_register_event_callbacks(panel_io,&callbacks,dma_complete)))return false;
 esp_lcd_panel_dev_config_t config{};config.reset_gpio_num=lcd_reset;
 config.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;config.bits_per_pixel=16;
 if(!screen_step("panel_create",esp_lcd_new_panel_st7789(panel_io,&config,&panel)))return false;
 if(!screen_step("panel_reset",esp_lcd_panel_reset(panel)))return false;
 if(!screen_step("panel_init",esp_lcd_panel_init(panel)))return false;
 // IDF provides reset/sleep-out/RGB565/RAMCTRL. Apply the board demo's
 // porch, voltage and gamma settings before exposing the framebuffer.
 struct Register { uint8_t command,length,data[14]; };
 static constexpr Register settings[]={
  {0xb2,5,{0x0c,0x0c,0x00,0x33,0x33}},
  {0xb7,1,{0x35}},{0xbb,1,{0x19}},{0xc0,1,{0x2c}},
  {0xc2,1,{0x01}},{0xc3,1,{0x12}},{0xc4,1,{0x20}},
  {0xc6,1,{0x0f}},{0xd0,2,{0xa4,0xa1}},
  {0xe0,14,{0xd0,0x04,0x0d,0x11,0x13,0x2b,0x3f,0x54,0x4c,0x18,0x0d,0x0b,0x1f,0x23}},
  {0xe1,14,{0xd0,0x04,0x0c,0x11,0x13,0x2c,0x3f,0x44,0x51,0x2f,0x1f,0x1f,0x20,0x23}},
 };
 for(const auto &r:settings)
  if(!screen_step("panel_timing",esp_lcd_panel_io_tx_param(panel_io,r.command,r.data,r.length)))return false;
 if(!screen_step("panel_invert",esp_lcd_panel_invert_color(panel,true)))return false;
 if(!screen_step("panel_swap",esp_lcd_panel_swap_xy(panel,true)))return false;
 if(!screen_step("panel_mirror",esp_lcd_panel_mirror(panel,true,false)))return false;
 // Keep the demo's ML scan-order bit as well as MX/MV. Orientation stays
 // fixed for this board, so the IDF mirror/swap setters are not used later.
 const uint8_t madctl=0x70;
 if(!screen_step("panel_scan",esp_lcd_panel_io_tx_param(panel_io,0x36,&madctl,1)))return false;
 if(!screen_step("panel_gap",esp_lcd_panel_set_gap(panel,40,53)))return false;
 if(!screen_step("panel_on",esp_lcd_panel_disp_on_off(panel,true)))return false;

 canvas=new lgfx::LGFX_Sprite();canvas->setColorDepth(16);canvas->setPsram(true);
 if(!screen_step("canvas",canvas->createSprite(screen_width,screen_height)?ESP_OK:ESP_ERR_NO_MEM))return false;
 screen_dma=static_cast<uint8_t*>(heap_caps_malloc(screen_width*stripe_rows*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
 if(!screen_step("dma_buffer",screen_dma?ESP_OK:ESP_ERR_NO_MEM))return false;
 return screen_step("ready",ESP_OK);
}

static void wake_inputs_init(){
 // Only sample BOOT after normal startup. No reset/download command is
 // attached to a press; holding BOOT during reset remains ROM recovery.
 gpio_config_t button{};button.pin_bit_mask=1ULL<<0;button.mode=GPIO_MODE_INPUT;
 button.pull_up_en=GPIO_PULLUP_ENABLE;button.pull_down_en=GPIO_PULLDOWN_DISABLE;
 button.intr_type=GPIO_INTR_DISABLE;boot_error=gpio_config(&button);
}

static bool local_display_activity(){
 return boot_error==ESP_OK && gpio_get_level(GPIO_NUM_0)==0;
}

static bool screen_present(){
 const auto *pixels=static_cast<const uint8_t*>(canvas->getBuffer());
 for(int y=0;y<screen_height;y+=stripe_rows){
  const int rows=std::min(stripe_rows,screen_height-y);
  // M5GFX's RGB565 sprite has the panel's big-endian byte order already.
  memcpy(screen_dma,pixels+y*screen_width*2,rows*screen_width*2);
  if(!screen_step("panel_draw",esp_lcd_panel_draw_bitmap(panel,0,y,screen_width,y+rows,screen_dma)))return false;
  // One outstanding stripe only. The ISR signals completion before this
  // DMA buffer is reused; a timeout stops painting instead of racing DMA.
  if(!screen_step("panel_wait",xSemaphoreTake(dma_complete,pdMS_TO_TICKS(300))==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT))return false;
 }
 return screen_step("ready",ESP_OK);
}

extern "C" void vibe_connected(bool value){wireless_usb(value);}
extern "C" void vibe_service(){}
extern "C" bool vibe_report(uint8_t *report){return wireless_report(report);}
extern "C" bool vibe_wheel(int8_t *wheel){return wireless_read_wheel(wheel);}
extern "C" void vibe_pcm(int16_t *out,size_t count){wireless_pcm(out,count);}
extern "C" void vibe_note_boot_intent(){}
extern "C" void vibe_request_power_reset(){} // GEEK has no M5PM1.
extern "C" uint8_t *vibe_snapshot(size_t *size){
 *size=0;if(!display_ok.load())return nullptr;
 const size_t bytes=screen_width*screen_height*2;
 auto *out=static_cast<uint8_t*>(malloc(bytes));
 if(out){
  xSemaphoreTake(screen_lock,portMAX_DELAY);memcpy(out,canvas->getBuffer(),bytes);
  xSemaphoreGive(screen_lock);*size=bytes;
 }
 return out;
}

extern "C" void vibe_status(char *out,size_t size){
 char radio[768];wireless_status(radio,sizeof(radio));
 char usb[384];vibe_usb_audio_status(usb,sizeof(usb));
 snprintf(out,size,
  "VIBE RX v=1 board=geek id=%s uptime=%lld width=%d height=%d screen=%d "
  "display_awake=%d display_idle_ms=%u display_timeout_ms=%u display_err=0x%x "
  "boot_err=0x%x mac_err=0x%x screen_driver=esp_lcd spi_hz=%d bl_pwm=%d "
  "screen_stage=%s screen_err=0x%x screen_errors=%u psram_size=%u "
  "heap_internal=%u main_stack_min=%u %s %s\n",
  short_id,esp_timer_get_time()/1000,screen_width,screen_height,int(display_ok.load()),
  int(display_awake.load()),unsigned(display_idle_ms.load()),unsigned(kDisplayIdleTimeoutMs),
  unsigned(display_power_error.load()),unsigned(boot_error),unsigned(mac_error),spi_clock_hz,
  display_awake.load()?backlight_percent:0,screen_stage.load(),unsigned(screen_error.load()),
  unsigned(screen_errors.load()),unsigned(esp_psram_get_size()),
  unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
  unsigned(uxTaskGetStackHighWaterMark(main_task)),radio,usb);
}

static void draw_screen(){
 if(!display_ok.load())return;
 wireless_view_t v;wireless_view(&v);
 xSemaphoreTake(screen_lock,portMAX_DELAY);
 auto &c=*canvas;
 const uint16_t bg=0x0842,card=0x10a4,muted=0x8410;
 const uint16_t accent=v.error!=ESP_OK?0xfba0:(v.connected?(v.listening?0xfd65:0x3ed7):0x8410);
 c.fillSprite(bg);c.setTextColor(0xffff,bg);c.setFont(&lgfx::fonts::Font2);
 c.drawString("STOPWATCH",8,3);c.setTextColor(muted,bg);
 char label[32];snprintf(label,sizeof(label),"GEEK %s",short_id);
 c.drawRightString(label,232,3);
 c.fillRoundRect(8,24,224,67,10,card);c.fillCircle(19,42,4,accent);
 c.setTextColor(accent,card);c.setFont(&lgfx::fonts::efontCN_24);
 c.drawString(v.error!=ESP_OK?"无线故障":(!v.initialized?"正在启动":
              (!v.connected?"等待连接":(v.listening?"正在讲话":"已连接"))),31,29);
 c.setTextColor(0xce79,card);c.setFont(&lgfx::fonts::efontCN_16);
 if(v.error!=ESP_OK){
  snprintf(label,sizeof(label),"WIFI 0x%X",unsigned(v.error));c.drawString(label,16,58);
 }else c.drawString(!v.initialized?"正在启动无线":(!v.connected?"请开启 StopWatch":
                      (v.listening?"松开黄色键结束":"按住黄色键讲话")),16,58);
 c.fillRoundRect(16,79,208,6,3,0x2945);
 const unsigned level=v.listening?std::min(208u,unsigned(v.level)*208/12000):0;
 if(level)c.fillRect(16,79,level,6,accent);
 c.setTextColor(0xce79,bg);
 if(v.connected)snprintf(label,sizeof(label),"表电池 %.2fV",v.battery_mv/1000.0);
 else snprintf(label,sizeof(label),"表电池 --");
 c.drawString(label,8,97);c.drawRightString(v.host?"USB 已连接":"等待 USB",232,97);
 c.setTextColor(muted,bg);c.setFont(&lgfx::fonts::Font2);
 snprintf(label,sizeof(label),"LOSS %u",unsigned(v.lost));c.drawString(label,8,117);
 c.drawRightString("48kHz MONO",232,117);
 display_ok=screen_present();
 xSemaphoreGive(screen_lock);
}

extern "C" void app_main(){
 main_task=xTaskGetCurrentTaskHandle();
 uint8_t mac[6]{};mac_error=esp_read_mac(mac,ESP_MAC_WIFI_STA);
 if(mac_error==ESP_OK)snprintf(short_id,sizeof(short_id),"%02X%02X",mac[4],mac[5]);
 display_ok=screen_init();wake_inputs_init();draw_screen();
 if(display_ok.load())screen_backlight(true);
 // A failed LCD never prevents the USB microphone/HID and radio from
 // coming up; diagnostic status remains available over USB CDC.
 vibe_usb_init();wireless_init();
 uint32_t now=uint32_t(esp_timer_get_time()/1000),last_draw=now,observed_activity=0;
 DisplayIdle idle(now);
 for(;;){
  now=uint32_t(esp_timer_get_time()/1000);
  wireless_view_t view;wireless_view(&view);
  const bool remote_activity=view.activity!=observed_activity || (view.connected && view.listening);
  observed_activity=view.activity;
  const bool awake=idle.update(now,local_display_activity() || remote_activity);
  display_idle_ms=idle.idle_ms(now);
  if(display_ok.load()){
   if(!awake){
    if(display_awake.load())screen_backlight(false);
   }else if(!display_awake.load() || uint32_t(now-last_draw)>=100){
    draw_screen();last_draw=now;
    if(display_ok.load() && !display_awake.load())screen_backlight(true);
   }
  }
  // LCD sleep does not stop USB, keyboard reports, wireless or audio.
  vTaskDelay(pdMS_TO_TICKS(50));
 }
}
