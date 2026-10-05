// USB descriptor templates and UAC control layout: TinyUSB, MIT License.
#include "tusb.h"
#include "vibe.h"
#include "maintenance.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"
#include "esp_private/periph_ctrl.h"
#include "hal/usb_serial_jtag_ll.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include <stdarg.h>
#include <stdatomic.h>

// Share one IN endpoint: ESP32-S3 has five IN endpoints including EP0.
enum { REPORT_KEYBOARD=1, REPORT_MOUSE=2 };
static const uint8_t hid_descriptor[] = {
 TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_KEYBOARD)),
 TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_MOUSE))
};
static const tusb_desc_device_t device = {
 .bLength=sizeof(tusb_desc_device_t), .bDescriptorType=TUSB_DESC_DEVICE,
 .bcdUSB=0x0200, .bDeviceClass=TUSB_CLASS_MISC, .bDeviceSubClass=MISC_SUBCLASS_COMMON,
 .bDeviceProtocol=MISC_PROTOCOL_IAD, .bMaxPacketSize0=64,
 .idVendor=0xcafe,
#ifdef VIBE_RECEIVER
 .idProduct=0x4021,
#else
 .idProduct=0x4020,
#endif
 .bcdDevice=0x0103,
 .iManufacturer=1, .iProduct=2, .iSerialNumber=3, .bNumConfigurations=1
};
enum { AUDIO_CONTROL, AUDIO_STREAM, KEYBOARD, CDC_CONTROL, CDC_DATA, INTERFACES };
#define TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO_MIC_ONE_CH_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)
static const uint8_t configuration[] = {
 TUD_CONFIG_DESCRIPTOR(1, INTERFACES, 0, TOTAL_LEN, 0, 500),
 TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR(AUDIO_CONTROL, 4, 2, 16, 0x81, CFG_TUD_AUDIO_EP_SZ_IN),
 TUD_HID_DESCRIPTOR(KEYBOARD, 5, HID_ITF_PROTOCOL_NONE, sizeof(hid_descriptor), 0x82, 16, 2),
 TUD_CDC_DESCRIPTOR(CDC_CONTROL, 6, 0x83, 8, 0x04, 0x84, 64),
};
_Static_assert(sizeof(configuration)==TOTAL_LEN,"USB descriptor length");
uint8_t const *tud_descriptor_device_cb(void) {return (const uint8_t*)&device;}
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {(void)index;return configuration;}
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {(void)instance;return hid_descriptor;}
uint16_t const *tud_descriptor_string_cb(uint8_t index,uint16_t langid) {
 (void)langid;
 static uint16_t result[64];
 #ifdef VIBE_RECEIVER
 static const char *strings[]={"", "Local prototype", "StopWatch Receiver C480", "9C139E8AC480-VIBE1", "StopWatch Wireless Microphone", "StopWatch Wireless Controls", "Diagnostics"};
#else
 static const char *strings[]={"", "Local prototype", "StopWatch Vibe", "288485439560-VIBE1", "StopWatch Vibe Microphone", "StopWatch Vibe Controls", "Diagnostics"};
#endif
 if(index==0){result[0]=0x0304;result[1]=0x0409;return result;}
 if(index>=sizeof(strings)/sizeof(strings[0]))return NULL;
 size_t n=strlen(strings[index]);if(n>63)n=63;
 result[0]=(TUSB_DESC_STRING<<8)|(2*n+2);
 for(size_t i=0;i<n;i++)result[i+1]=(uint8_t)strings[index][i];
 return result;
}
static uint8_t last_hid[8];
uint16_t tud_hid_get_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t *buffer,uint16_t length) {
 (void)instance;(void)id;
 if(type!=HID_REPORT_TYPE_INPUT)return 0;
 if(id==REPORT_MOUSE){uint16_t n=length<5?length:5;memset(buffer,0,n);return n;}
 if(id!=REPORT_KEYBOARD)return 0;
 uint16_t n=length<8?length:8;memcpy(buffer,last_hid,n);return n;
}
void tud_hid_set_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,const uint8_t *buffer,uint16_t size) {
 (void)instance;(void)id;(void)type;(void)buffer;(void)size;
}
static _Atomic unsigned mounts,unmounts,suspends,resumes;
// Diagnostic counters only; packet generation and scheduling stay unchanged.
static _Atomic uint32_t usb_audio_frames,usb_audio_load_bytes;
static _Atomic uint32_t usb_audio_p94,usb_audio_p96,usb_audio_p98,usb_audio_pother;
static _Atomic uint32_t usb_audio_last_us,usb_audio_gap_max_us,usb_audio_gaps,usb_audio_short;
static _Atomic uint32_t usb_audio_alt;
uint32_t vibe_usb_lifecycle(void){
 return (atomic_load(&mounts)&255) | ((atomic_load(&unmounts)&255)<<8)
  | ((atomic_load(&suspends)&255)<<16) | ((atomic_load(&resumes)&255)<<24);
}
void tud_mount_cb(void){++mounts;atomic_store_explicit(&usb_audio_last_us,0,memory_order_relaxed);atomic_store_explicit(&usb_audio_alt,0,memory_order_relaxed);vibe_connected(true);}
void tud_umount_cb(void){++unmounts;atomic_store_explicit(&usb_audio_last_us,0,memory_order_relaxed);atomic_store_explicit(&usb_audio_alt,0,memory_order_relaxed);vibe_connected(false);}
void tud_suspend_cb(bool wake){(void)wake;++suspends;atomic_store_explicit(&usb_audio_last_us,0,memory_order_relaxed);vibe_connected(false);}
void tud_resume_cb(void){++resumes;atomic_store_explicit(&usb_audio_last_us,0,memory_order_relaxed);vibe_connected(true);}

void vibe_usb_audio_status(char *out,size_t size){
 // Post-load bytes are scheduled for the endpoint, not a host receipt count.
 snprintf(out,size,"uac_alt=%u uac_pre=%u uac_load_bytes=%u uac_p94=%u uac_p96=%u uac_p98=%u uac_pother=%u uac_gap_max_us=%u uac_gap_gt1500=%u uac_short=%u",
  (unsigned)atomic_load_explicit(&usb_audio_alt,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_frames,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_load_bytes,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_p94,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_p96,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_p98,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_pother,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_gap_max_us,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_gaps,memory_order_relaxed),
  (unsigned)atomic_load_explicit(&usb_audio_short,memory_order_relaxed));
}

bool tud_audio_set_itf_cb(uint8_t port,const tusb_control_request_t *r){
 (void)port;
 if((r->wIndex&255)==AUDIO_STREAM){
  atomic_store_explicit(&usb_audio_last_us,0,memory_order_relaxed);
  atomic_store_explicit(&usb_audio_alt,r->wValue&255,memory_order_relaxed);
 }
 return true;
}
bool tud_audio_set_itf_close_EP_cb(uint8_t port,const tusb_control_request_t *r){
 (void)port;
 // wValue is the requested new alt; this callback closes the previous EP.
 if((r->wIndex&255)==AUDIO_STREAM){
  atomic_store_explicit(&usb_audio_last_us,0,memory_order_relaxed);
  atomic_store_explicit(&usb_audio_alt,0,memory_order_relaxed);
 }
 return true;
}

static uint32_t rate=48000;
static uint8_t clock_valid=1, muted[2]={0};
static int16_t volume[2]={0};
static int32_t scale=32768;
bool tud_audio_set_req_entity_cb(uint8_t port,const tusb_control_request_t *r,uint8_t *data) {
 (void)port;uint8_t entity=r->wIndex>>8, ctrl=r->wValue>>8,channel=r->wValue&255;
 if(r->bRequest!=AUDIO_CS_REQ_CUR || channel>1)return false;
 if(entity==2 && ctrl==AUDIO_FU_CTRL_MUTE && r->wLength==1){muted[channel]=data[0]!=0;return true;}
 if(entity==2 && ctrl==AUDIO_FU_CTRL_VOLUME && r->wLength==2){
  int16_t value;memcpy(&value,data,2);if(value>0 || value<(-60*256))return false;
  volume[channel]=value;scale=(int32_t)(32768.0f*powf(10.0f,(volume[0]+volume[1])/5120.0f));return true;
 }
 if(entity==4 && ctrl==AUDIO_CS_CTRL_SAM_FREQ && r->wLength==4){uint32_t v;memcpy(&v,data,4);return v==48000;}
 return false;
}
bool tud_audio_get_req_entity_cb(uint8_t port,const tusb_control_request_t *r) {
 uint8_t entity=r->wIndex>>8, ctrl=r->wValue>>8,channel=r->wValue&255;
 if(channel>1)return false;
 if(entity==4 && ctrl==AUDIO_CS_CTRL_SAM_FREQ){
  if(r->bRequest==AUDIO_CS_REQ_CUR)return tud_control_xfer(port,r,&rate,4);
  if(r->bRequest==AUDIO_CS_REQ_RANGE){audio_control_range_4_n_t(1) range={.wNumSubRanges=1,.subrange={{48000,48000,0}}};return tud_audio_buffer_and_schedule_control_xfer(port,r,&range,sizeof(range));}
 }
 if(entity==4 && ctrl==AUDIO_CS_CTRL_CLK_VALID)return tud_control_xfer(port,r,&clock_valid,1);
 if(entity==2 && ctrl==AUDIO_FU_CTRL_MUTE)return tud_control_xfer(port,r,&muted[channel],1);
 if(entity==2 && ctrl==AUDIO_FU_CTRL_VOLUME){
  if(r->bRequest==AUDIO_CS_REQ_CUR)return tud_control_xfer(port,r,&volume[channel],2);
  if(r->bRequest==AUDIO_CS_REQ_RANGE){audio_control_range_2_n_t(1) range={.wNumSubRanges=1,.subrange={{-60*256,0,256}}};return tud_audio_buffer_and_schedule_control_xfer(port,r,&range,sizeof(range));}
 }
 if(entity==1 && ctrl==AUDIO_TE_CTRL_CONNECTOR){audio_desc_channel_cluster_t cluster={.bNrChannels=1,.bmChannelConfig=0,.iChannelNames=0};return tud_audio_buffer_and_schedule_control_xfer(port,r,&cluster,sizeof(cluster));}
 return false;
}
bool tud_audio_tx_done_pre_load_cb(uint8_t port,uint8_t itf,uint8_t ep,uint8_t alt) {
 (void)port;(void)itf;(void)ep;(void)alt;
 uint32_t now=(uint32_t)esp_timer_get_time();
 uint32_t previous=atomic_exchange_explicit(&usb_audio_last_us,now,memory_order_relaxed);
 if(previous){
  uint32_t gap=now-previous;
  if(gap>atomic_load_explicit(&usb_audio_gap_max_us,memory_order_relaxed))atomic_store_explicit(&usb_audio_gap_max_us,gap,memory_order_relaxed);
  if(gap>1500)atomic_fetch_add_explicit(&usb_audio_gaps,1,memory_order_relaxed);
 }
 atomic_fetch_add_explicit(&usb_audio_frames,1,memory_order_relaxed);
 int16_t data[48];vibe_pcm(data,48);
 if(muted[0]||muted[1])memset(data,0,sizeof(data));
 else if(scale!=32768)for(unsigned i=0;i<48;i++)data[i]=(int32_t)data[i]*scale/32768;
 if(tud_audio_write(data,sizeof(data))!=sizeof(data))atomic_fetch_add_explicit(&usb_audio_short,1,memory_order_relaxed);
 return true;
}
bool tud_audio_tx_done_post_load_cb(uint8_t port,uint16_t bytes,uint8_t itf,uint8_t ep,uint8_t alt){
 (void)port;(void)itf;(void)ep;(void)alt;
 atomic_fetch_add_explicit(&usb_audio_load_bytes,bytes,memory_order_relaxed);
 _Atomic uint32_t *counter=bytes==94?&usb_audio_p94:(bytes==96?&usb_audio_p96:(bytes==98?&usb_audio_p98:&usb_audio_pother));
 atomic_fetch_add_explicit(counter,1,memory_order_relaxed);
 return true;
}

static void control_task(void *arg) {
 (void)arg; bool pending=false; uint8_t report[8]; uint32_t last=0;
 bool mouse_pending=false;int8_t wheel=0;
 uint8_t *snapshot=NULL;size_t snap_size=0,snap_pos=0;
 maintenance_t maintenance={0};
 for(;;) {
  vibe_service();
  if(!tud_mounted() || tud_suspended()){pending=false;mouse_pending=false;}
  if(!tud_cdc_connected())memset(&maintenance,0,sizeof(maintenance));
  if(!pending && tud_mounted())pending=vibe_report(report);
  if(pending && tud_hid_ready() && tud_hid_report(REPORT_KEYBOARD,report,8)) {
   memcpy(last_hid,report,8);pending=false;
  }
  if(!mouse_pending && tud_mounted() && !tud_suspended())mouse_pending=vibe_wheel(&wheel);
  if(mouse_pending && tud_hid_ready() && tud_hid_mouse_report(REPORT_MOUSE,0,0,0,wheel,0))mouse_pending=false;
  while(tud_cdc_available()) {
   char c=tud_cdc_read_char();
   if(c=='P' && !snapshot){snapshot=vibe_snapshot(&snap_size);snap_pos=0;
    if(snapshot){char h[64];int n=snprintf(h,sizeof(h),
#ifdef VIBE_RECEIVER
     "FRAME 320 240 %u\n",(unsigned)snap_size
#else
     "FRAME 360 360 %u\n",(unsigned)snap_size
#endif
);tud_cdc_write(h,n);tud_cdc_write_flush();}}
   maintenance_action_t action=maintenance_byte(&maintenance,c,pdTICKS_TO_MS(xTaskGetTickCount()));
   if(action==MAINT_POWER_RESET)vibe_request_power_reset();
   if(action==MAINT_BOOT) {
    vibe_note_boot_intent();
    tud_disconnect();vTaskDelay(pdMS_TO_TICKS(50));
    periph_module_reset(PERIPH_USB_MODULE);periph_module_disable(PERIPH_USB_MODULE);
    // Runtime USJ is disabled; explicitly restore it only for ROM maintenance.
    PERIPH_RCC_ATOMIC(){usb_serial_jtag_ll_enable_bus_clock(true);}
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,RTC_CNTL_SW_HW_USB_PHY_SEL|RTC_CNTL_SW_USB_PHY_SEL|RTC_CNTL_USB_PAD_ENABLE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG,USB_SERIAL_JTAG_PHY_SEL);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG,USB_SERIAL_JTAG_USB_PAD_ENABLE);
    REG_WRITE(RTC_CNTL_OPTION1_REG,RTC_CNTL_FORCE_DOWNLOAD_BOOT);esp_restart();
   }
  }
  uint32_t now=xTaskGetTickCount();
  if(snapshot){
   if(!tud_cdc_connected()){free(snapshot);snapshot=NULL;}
   else{size_t n=tud_cdc_write_available();if(n>snap_size-snap_pos)n=snap_size-snap_pos;
    snap_pos+=tud_cdc_write(snapshot+snap_pos,n);tud_cdc_write_flush();
    if(snap_pos==snap_size){free(snapshot);snapshot=NULL;}}
  }
  else if(tud_cdc_connected() && now-last>=1000 && tud_cdc_write_available()>1536) {
   last=now; char line[1536]; vibe_status(line,sizeof(line));
   tud_cdc_write(line,strlen(line));tud_cdc_write_flush();
  }
  vTaskDelay(pdMS_TO_TICKS(1));
 }
}
static void usb_task(void *arg){(void)arg;for(;;)tud_task();}
void vibe_usb_init(void) {
 usb_phy_handle_t phy;usb_phy_config_t cfg={.controller=USB_PHY_CTRL_OTG,.target=USB_PHY_TARGET_INT,.otg_mode=USB_OTG_MODE_DEVICE};
 ESP_ERROR_CHECK(usb_new_phy(&cfg,&phy));
 // OTG now owns the internal PHY. Normalize even CPU-only reset paths, where
 // IDF startup preserves peripheral clocks instead of applying USJ=n again.
 PERIPH_RCC_ATOMIC(){
  usb_serial_jtag_ll_enable_bus_clock(true);
  usb_serial_jtag_ll_phy_enable_pad(false);
  usb_serial_jtag_ll_enable_bus_clock(false);
 }
 assert(tusb_init());
#ifdef VIBE_RECEIVER
 // Isochronous packets have a 1 ms deadline. Keep their worker off Wi-Fi's
 // core 0 and above TCP/IP work; tud_task() blocks when no USB event is pending.
 xTaskCreatePinnedToCore(usb_task,"usb",6144,NULL,configMAX_PRIORITIES-1,NULL,1);
#else
 xTaskCreatePinnedToCore(usb_task,"usb",6144,NULL,8,NULL,0);
#endif
 xTaskCreatePinnedToCore(control_task,"controls",6144,NULL,4,NULL,0);
}
