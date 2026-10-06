#include "wireless.h"
#include "wire_protocol.hpp"
#include "station_reconnect.hpp"
#include "pairing.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <algorithm>
using namespace wire;
static constexpr int control_port=38470,audio_port=38471;
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> wifi_up{false},peer_usb{false},local_usb{false},queue_fault{false};
static std::atomic<uint32_t> seen{0},token{0},audio_seen{0},tx_audio_count{0},rx_audio_count{0},tx_errors{0},connections{0};
static std::atomic<uint32_t> input_activity{0};
#ifdef VIBE_RECEIVER
struct KeyEvent{uint32_t epoch;uint8_t keys[8];};
struct WheelEvent{uint32_t epoch;int8_t wheel;};
static QueueHandle_t keys_out,wheels_out;
static uint32_t usb_epoch=0; // invalidated by USB or control-link loss; guarded
static bool receiver_armed=false;
#else
static QueueHandle_t controls,audios;
#endif
static std::atomic<bool> init_requested{false},radio_running{false};
static std::atomic<esp_err_t> radio_error{ESP_OK};
static std::atomic<int> radio_errno{0};
static std::atomic<uint32_t> init_stack_min{UINT32_MAX};
static std::atomic<const char*> radio_stage{"off"};
static esp_netif_t *radio_netif=nullptr;
#ifndef VIBE_RECEIVER
static std::atomic<uint32_t> wifi_disconnects{0},wifi_attempts{0},wifi_timeouts{0},tcp_attempts{0};
static std::atomic<int> wifi_reason{0},wifi_connect_error{0},tcp_error{0};
static std::atomic<uint32_t> tcp_failure_run{0},tcp_failure_ms{0},tcp_recoveries{0};
static std::atomic<int> tcp_disconnect_error{0};
// Only the control worker calls the driver. The event loop publishes its latest
// transition under guard; wifi_up is updated in the same critical section.
static StationEvent station_event=StationEvent::none;
static uint32_t station_event_sequence=0;
static StationEventGate station_event_gate;
#endif
static esp_err_t radio_fail(const char *stage,esp_err_t error,int detail=0){
 esp_err_t expected=ESP_OK;
 if(radio_error.compare_exchange_strong(expected,error)){
  radio_stage=stage;radio_errno=detail;
 }
 radio_running=false;wifi_up=false;peer_usb=false;token=0;
 return error;
}
static Control state;
static AudioBuffer audio_buffer;
#ifdef VIBE_RECEIVER
static uint8_t delivered_keys[8]{};
static uint32_t delivered_epoch=0;
#endif
static uint32_t now_ms(){return uint32_t(esp_timer_get_time()/1000);}
static bool link_alive(){return radio_running.load() && token.load()!=0 && fresh(now_ms(),seen.load());}
extern "C" bool wireless_ready(){return link_alive() && peer_usb.load();}
#ifdef VIBE_RECEIVER
static void clear_receiver(){
 portENTER_CRITICAL(&guard);++usb_epoch;receiver_armed=false;state={};audio_buffer.clear();audio_seen=0;portEXIT_CRITICAL(&guard);
 // Queues are published only after all initialization steps have succeeded.
 if(!radio_running.load())return;
 if(keys_out)xQueueReset(keys_out);
 if(wheels_out)xQueueReset(wheels_out);
}
#endif
static bool socket_timeouts(int fd){
 timeval timeout{0,150000};
 return setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0
  && setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))==0;
}
static bool options(int fd){
 int one=1;
 return socket_timeouts(fd) && setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one))==0;
}
#ifdef VIBE_RECEIVER
static bool retryable_socket_error(){return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR || errno==ETIMEDOUT;}
#endif
#ifndef VIBE_RECEIVER
// lwIP's send timeout does not bound blocking connect(). Poll a nonblocking
// connection instead, so a radio fault also stops an unreachable-peer attempt.
static bool connect_peer(int fd,const sockaddr_in &remote){
 int flags=fcntl(fd,F_GETFL,0);
 if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)return false;
 int result=connect(fd,(const sockaddr*)&remote,sizeof(remote));
 if(result<0 && errno!=EINPROGRESS)return false;
 if(result<0){
  fd_set write_set;FD_ZERO(&write_set);FD_SET(fd,&write_set);timeval timeout{0,150000};
  int selected=select(fd+1,nullptr,&write_set,nullptr,&timeout);
  if(selected<=0){if(selected==0)errno=ETIMEDOUT;return false;}
  int error=0;socklen_t size=sizeof(error);
  if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)<0)return false;
  if(error){errno=error;return false;}
 }
 if(!radio_running.load() || !wifi_up.load()){errno=ENETDOWN;return false;}
 return fcntl(fd,F_SETFL,flags)==0;
}
#endif
static bool transfer(int fd,void *data,size_t size,bool writing){
 auto *p=static_cast<uint8_t*>(data);
 while(size){if(!radio_running.load())return false;int n=writing?send(fd,p,size,0):recv(fd,p,size,0);if(n<=0)return false;p+=n;size-=n;}
 return true;
}
static sockaddr_in address(int port,const char *ip){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=inet_addr(ip);return a;}
static void wifi_event(void*,esp_event_base_t base,int32_t id,void *data){
#ifndef VIBE_RECEIVER
 if(radio_error.load()!=ESP_OK)return;
 StationEvent event=StationEvent::none;
 if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START)event=StationEvent::started;
 if(base==WIFI_EVENT && id==WIFI_EVENT_STA_STOP)event=StationEvent::stopped;
 if(base==WIFI_EVENT && id==WIFI_EVENT_STA_CONNECTED)event=StationEvent::associated;
 if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
  ++wifi_disconnects;if(data)wifi_reason=static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
  event=StationEvent::disconnected;
 }
 bool matching_netif=true;
 if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){
  event=StationEvent::got_ip;
  // radio_netif is published before handlers are registered or Wi-Fi starts.
  matching_netif=data && radio_netif && static_cast<ip_event_got_ip_t*>(data)->esp_netif==radio_netif;
 }
 if(event!=StationEvent::none){
  portENTER_CRITICAL(&guard);
  if(!station_event_gate.accept(event,matching_netif)){portEXIT_CRITICAL(&guard);return;}
  if(event==StationEvent::got_ip)wifi_up=true;
  if(event==StationEvent::started || event==StationEvent::disconnected || event==StationEvent::stopped){wifi_up=false;peer_usb=false;}
  station_event=event;++station_event_sequence;
  portEXIT_CRITICAL(&guard);
 }
#else
 (void)base;(void)id;(void)data;
#endif
}
#ifndef VIBE_RECEIVER
static void service_station(StationReconnect &policy,uint32_t &observed_sequence){
 StationEvent event;uint32_t sequence;
 portENTER_CRITICAL(&guard);event=station_event;sequence=station_event_sequence;portEXIT_CRITICAL(&guard);
 uint32_t now=now_ms();
 if(sequence!=observed_sequence){observed_sequence=sequence;policy.event(event,now);}
 auto action=policy.poll(now,wifi_up.load());
 if(action==StationReconnect::Action::connect){
  portENTER_CRITICAL(&guard);bool unchanged=sequence==station_event_sequence;portEXIT_CRITICAL(&guard);
  if(!unchanged || wifi_up.load())return;
  ++wifi_attempts;esp_err_t error=esp_wifi_connect();wifi_connect_error=error;
  // STATE means the driver is already scanning/connecting; leave it time to
  // finish instead of repeatedly submitting a competing request.
  policy.connect_result(error==ESP_OK || error==ESP_ERR_WIFI_STATE,now_ms());
 }else if(action==StationReconnect::Action::timeout){
  // A default GOT_IP post can fail if the event queue is full. Reconcile with
  // actual association/netif state before cancelling a stalled attempt.
  wifi_ap_record_t ap{};esp_netif_ip_info_t ip{};esp_netif_dhcp_status_t dhcp{};
  bool associated=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
  bool ready=associated && radio_netif
   && esp_netif_is_netif_up(radio_netif)
   && esp_netif_get_ip_info(radio_netif,&ip)==ESP_OK
   && esp_netif_dhcpc_get_status(radio_netif,&dhcp)==ESP_OK
   && dhcp==ESP_NETIF_DHCP_STOPPED && ip.ip.addr==inet_addr("192.168.7.2");
  portENTER_CRITICAL(&guard);
  bool unchanged=sequence==station_event_sequence;
  if(unchanged && ready)wifi_up=true;
  portEXIT_CRITICAL(&guard);
  if(!unchanged)return; // Process a newer driver event on the next worker turn.
  if(ready){policy.event(StationEvent::got_ip,now_ms());return;}
  // Association is real even if IP diagnostics failed. Do not disrupt that
  // link; keep waiting for/default-netif diagnostics instead of reconnecting.
  if(associated){policy.event(StationEvent::associated,now_ms());return;}
  ++wifi_timeouts;
  // Cancel only after 15 seconds without completion. Never stop/start Wi-Fi:
  // STA_STOP would reset the static-IP netif to DHCP_INIT.
  esp_wifi_disconnect();
 }
}
static void publish_keys(const uint8_t *keys,int8_t wheel){
 Control p;
 portENTER_CRITICAL(&guard);if(keys)memcpy(state.keys,keys,8);p=state;portEXIT_CRITICAL(&guard);
 p.wheel=wheel;
 if(wireless_ready() && controls && xQueueSend(controls,&p,0)!=pdTRUE)queue_fault=true;
}
extern "C" void wireless_keyboard(const uint8_t *keys){publish_keys(keys,0);}
extern "C" void wireless_wheel(int8_t wheel){publish_keys(nullptr,wheel);}
extern "C" void wireless_state(bool listening,uint32_t session,uint16_t battery,float level){
 portENTER_CRITICAL(&guard);state.listening=listening;state.session=session;state.battery_mv=battery;
 state.level=uint16_t(std::min(1.f,std::max(0.f,level))*65535);portEXIT_CRITICAL(&guard);
}
extern "C" void wireless_audio(const int16_t *pcm,size_t n){
 static uint32_t sequence=0;
 if(n!=audio_samples || !wireless_ready() || !audios)return;
 Audio p;p.token=token.load();p.sequence=++sequence;
 portENTER_CRITICAL(&guard);p.session=state.session;bool listen=state.listening;portEXIT_CRITICAL(&guard);
 if(!listen)return;
 memcpy(p.pcm,pcm,sizeof(p.pcm));
 if(xQueueSend(audios,&p,0)!=pdTRUE)++tx_errors;
}
static void sender_audio(void*){
 int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
 if(fd<0){radio_fail("audio_socket",ESP_FAIL,errno);return;}
 if(!socket_timeouts(fd)){int error=errno;close(fd);radio_fail("audio_options",ESP_FAIL,error);return;}
 auto remote=address(audio_port,"192.168.7.1");Audio p;
 while(radio_running.load()){if(xQueueReceive(audios,&p,pdMS_TO_TICKS(100))!=pdTRUE)continue;
  if(!wireless_ready() || p.token!=token.load())continue;
  portENTER_CRITICAL(&guard);bool valid_session=state.listening && p.session==state.session;portEXIT_CRITICAL(&guard);
  if(!valid_session)continue;
  if(sendto(fd,&p,sizeof(p),0,(sockaddr*)&remote,sizeof(remote))==sizeof(p))++tx_audio_count;else ++tx_errors;
 }
 close(fd);
}
static void sender_control(void*){
 StationReconnect reconnect;uint32_t observed_sequence=0;
 ControlLinkRecovery peer_health;
 auto healthy=[&](){peer_health.healthy();tcp_failure_run=0;tcp_failure_ms=0;};
 auto failed=[&](){
  uint32_t now=now_ms();bool recover=peer_health.failed(now,wifi_up.load());
  tcp_failure_run=peer_health.failures();tcp_failure_ms=peer_health.failure_ms(now);
  if(!recover)return;
  // Invalidate the old association immediately, including any delayed IP
  // event, even if the driver's disconnect completion is itself missing.
  portENTER_CRITICAL(&guard);
  bool still_up=wifi_up.load();
  if(still_up){
   wifi_up=false;peer_usb=false;token=0;
   station_event_gate.accept(StationEvent::disconnected);
   station_event=StationEvent::disconnected;++station_event_sequence;
  }
  portEXIT_CRITICAL(&guard);
  if(still_up){++tcp_recoveries;tcp_disconnect_error=esp_wifi_disconnect();}
 };
 // The checked startup completed even if its STA_START event was lost.
 reconnect.event(StationEvent::started,now_ms());
 while(radio_running.load()){
  peer_usb=false;token=0;xQueueReset(controls);xQueueReset(audios);queue_fault=false;
  service_station(reconnect,observed_sequence);
  if(!wifi_up){healthy();vTaskDelay(pdMS_TO_TICKS(100));continue;}
  int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(fd<0){vTaskDelay(pdMS_TO_TICKS(100));continue;}
  if(!options(fd)){int error=errno;close(fd);radio_fail("control_options",ESP_FAIL,error);break;}
  auto remote=address(control_port,"192.168.7.1");
  ++tcp_attempts;
  if(!connect_peer(fd,remote)){tcp_error=errno;close(fd);failed();vTaskDelay(pdMS_TO_TICKS(250));continue;}
  tcp_error=0;
  Ack ack;
  if(!transfer(fd,&ack,sizeof(ack),false)||ack.magic_value!=magic||!ack.token){close(fd);failed();vTaskDelay(pdMS_TO_TICKS(50));continue;}
  if(!radio_running.load()){close(fd);break;}
  healthy(); // TCP connect alone does not demonstrate a functioning receiver.
  token=ack.token;seen=now_ms();peer_usb=ack.usb!=0;++connections;
  bool control_failed=false;
  while(radio_running.load() && wifi_up && !queue_fault){
   Control p;
   if(xQueueReceive(controls,&p,pdMS_TO_TICKS(20))!=pdTRUE){portENTER_CRITICAL(&guard);p=state;portEXIT_CRITICAL(&guard);}
   p.token=token.load();
   // Release can race with report generation; never advertise speech with no Option.
   if(!(p.keys[0]&0x40))p.listening=0;
   if(!transfer(fd,&p,sizeof(p),true)||!transfer(fd,&ack,sizeof(ack),false)||ack.magic_value!=magic||ack.token!=token.load()){control_failed=true;break;}
   healthy();
   seen=now_ms();peer_usb=ack.usb!=0;
  }
  peer_usb=false;token=0;close(fd);
  if(control_failed && radio_running.load())failed();
  vTaskDelay(pdMS_TO_TICKS(50));
 }
 peer_usb=false;token=0;
}
extern "C" void wireless_usb(bool){}
extern "C" bool wireless_report(uint8_t*){return false;}
extern "C" bool wireless_read_wheel(int8_t*){return false;}
extern "C" void wireless_pcm(int16_t *out,size_t n){memset(out,0,n*2);}
#else
extern "C" void wireless_state(bool,uint32_t,uint16_t,float){}
extern "C" void wireless_keyboard(const uint8_t*){}
extern "C" void wireless_wheel(int8_t){}
extern "C" void wireless_audio(const int16_t*,size_t){}
extern "C" void wireless_usb(bool value){
 portENTER_CRITICAL(&guard);
 local_usb=value;
 if(!value){++usb_epoch;receiver_armed=false;state={};audio_buffer.clear();audio_seen=0;}
 portEXIT_CRITICAL(&guard);
}
extern "C" bool wireless_report(uint8_t *keys){
 portENTER_CRITICAL(&guard);
 // A resume can rearm before USB polls again. Release the preceding epoch
 // first, without consuming a new press that arrived in the current epoch.
 if(delivered_epoch!=usb_epoch){
  delivered_epoch=usb_epoch;memset(delivered_keys,0,8);memset(keys,0,8);
  portEXIT_CRITICAL(&guard);return true;
 }
 bool active=link_alive() && local_usb && receiver_armed;
 if(!active){
  uint8_t zero[8]{};bool changed=memcmp(delivered_keys,zero,8)!=0;
  if(changed){memset(delivered_keys,0,8);memset(keys,0,8);}
  portEXIT_CRITICAL(&guard);return changed;
 }
 // Zero-wait receive is serialized with the epoch check; no thread blocks
 // while holding guard, and the queue producer never holds guard at send.
 KeyEvent event{};bool valid_event=false;
 while(keys_out && xQueueReceive(keys_out,&event,0)==pdTRUE){
  if(event.epoch!=usb_epoch)continue;
  memcpy(keys,event.keys,8);memcpy(delivered_keys,keys,8);valid_event=true;break;
 }
 portEXIT_CRITICAL(&guard);return valid_event;
}
extern "C" bool wireless_read_wheel(int8_t *wheel){
 WheelEvent event{};if(!radio_running.load() || !wheels_out || xQueueReceive(wheels_out,&event,0)!=pdTRUE)return false;
 portENTER_CRITICAL(&guard);
 bool valid_event=link_alive() && local_usb && receiver_armed && event.epoch==usb_epoch;
 if(valid_event)*wheel=event.wheel;
 portEXIT_CRITICAL(&guard);return valid_event;
}
extern "C" void wireless_pcm(int16_t *out,size_t n){
 portENTER_CRITICAL(&guard);
 bool alive=link_alive() && local_usb && receiver_armed && uint32_t(now_ms()-audio_seen.load())<80;
 if(!alive || !state.listening){audio_buffer.clear();memset(out,0,n*2);}else audio_buffer.read(out,n);
 portEXIT_CRITICAL(&guard);
}
static void receiver_audio(void*){
 int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
 if(fd<0){radio_fail("audio_socket",ESP_FAIL,errno);return;}
 if(!socket_timeouts(fd)){int error=errno;close(fd);radio_fail("audio_options",ESP_FAIL,error);return;}
 auto local=address(audio_port,"0.0.0.0");
 if(bind(fd,(sockaddr*)&local,sizeof(local))){int error=errno;close(fd);radio_fail("audio_bind",ESP_FAIL,error);return;}
 Audio p;uint8_t datagram[sizeof(Audio)+1];
 while(radio_running.load()){sockaddr_in source{};socklen_t len=sizeof(source);int n=recvfrom(fd,datagram,sizeof(datagram),0,(sockaddr*)&source,&len);
  if(n<0){if(retryable_socket_error())continue;radio_fail("audio_recv",ESP_FAIL,errno);break;}
  if(n!=sizeof(p))continue;
  memcpy(&p,datagram,sizeof(p));
  if(n!=sizeof(p)||source.sin_addr.s_addr!=inet_addr("192.168.7.2")||p.magic_value!=magic||!link_alive()||!local_usb||p.token!=token.load())continue;
  portENTER_CRITICAL(&guard);
  if(receiver_armed && local_usb && link_alive() && p.token==token.load() && state.listening && p.session==state.session && audio_buffer.push(p)){audio_seen=now_ms();++rx_audio_count;}
  portEXIT_CRITICAL(&guard);
 }
 close(fd);
}
static void receiver_control(void*){
 int listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
 if(listener<0){radio_fail("control_socket",ESP_FAIL,errno);return;}
 if(!socket_timeouts(listener)){int error=errno;close(listener);radio_fail("listen_options",ESP_FAIL,error);return;}
 int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
 auto local=address(control_port,"0.0.0.0");
 if(bind(listener,(sockaddr*)&local,sizeof(local))){int error=errno;close(listener);radio_fail("control_bind",ESP_FAIL,error);return;}
 if(listen(listener,1)){int error=errno;close(listener);radio_fail("control_listen",ESP_FAIL,error);return;}
 while(radio_running.load()){token=0;peer_usb=false;clear_receiver();sockaddr_in source{};socklen_t len=sizeof(source);int fd=accept(listener,(sockaddr*)&source,&len);
  if(fd<0){if(retryable_socket_error())continue;radio_fail("control_accept",ESP_FAIL,errno);break;}
  if(!radio_running.load()){close(fd);break;}
  if(source.sin_addr.s_addr!=inet_addr("192.168.7.2")){close(fd);continue;}
  if(!options(fd)){int error=errno;close(fd);radio_fail("control_options",ESP_FAIL,error);break;}
  uint32_t t;do{t=esp_random();}while(!t);token=t;++connections;
  Ack ack;ack.token=t;ack.usb=local_usb && radio_running.load();
  if(!transfer(fd,&ack,sizeof(ack),true)){close(fd);continue;}
  uint8_t previous[8]{};
  portENTER_CRITICAL(&guard);uint32_t observed_epoch=usb_epoch;receiver_armed=false;portEXIT_CRITICAL(&guard);
  while(radio_running.load()){Control p;if(!transfer(fd,&p,sizeof(p),false)||!valid(p)||p.token!=t)break;
   if(!radio_running.load())break;
   seen=now_ms();peer_usb=local_usb.load();
   uint8_t zero[8]{};
   portENTER_CRITICAL(&guard);
   if(control_activity(p,state))++input_activity;
   if(observed_epoch!=usb_epoch){observed_epoch=usb_epoch;receiver_armed=false;memset(previous,0,8);}
   if(!local_usb)receiver_armed=false;
   if(local_usb && !memcmp(p.keys,zero,8) && !p.listening)receiver_armed=true;
   if(!receiver_armed){memset(p.keys,0,8);p.wheel=0;p.listening=0;}
   uint32_t packet_epoch=usb_epoch;
   if(!p.listening || p.session!=state.session)audio_buffer.clear();
   state=p;portEXIT_CRITICAL(&guard);
   if(memcmp(p.keys,previous,8)){
    KeyEvent event{packet_epoch,{}};memcpy(event.keys,p.keys,8);
    if(xQueueSend(keys_out,&event,0)!=pdTRUE)break;
    memcpy(previous,p.keys,8);
   }
   if(p.wheel){WheelEvent event{packet_epoch,p.wheel};if(xQueueSend(wheels_out,&event,0)!=pdTRUE)break;}
   ack.usb=local_usb && radio_running.load();
   if(!transfer(fd,&ack,sizeof(ack),true))break;
  }
  token=0;peer_usb=false;clear_receiver();close(fd);
 }
 token=0;peer_usb=false;clear_receiver();close(listener);
}
#endif
extern "C" void wireless_view(wireless_view_t *v){
 v->connected=link_alive();v->initialized=radio_running.load();v->error=radio_error.load();v->stage=radio_stage.load();
#ifdef VIBE_RECEIVER
 v->host=local_usb.load();
#else
 v->host=peer_usb.load();
#endif
 v->received=rx_audio_count.load();v->activity=input_activity.load();
 portENTER_CRITICAL(&guard);v->listening=v->connected && state.listening;v->battery_mv=state.battery_mv;v->level=state.level;v->lost=audio_buffer.lost;portEXIT_CRITICAL(&guard);
}
extern "C" void wireless_status(char *out,size_t size){
 Control p;unsigned lost,over,under,buffered;
 portENTER_CRITICAL(&guard);p=state;lost=audio_buffer.lost;over=audio_buffer.overflow;under=audio_buffer.underflow;buffered=audio_buffer.count;portEXIT_CRITICAL(&guard);
 wireless_view_t view;wireless_view(&view);
 char wifi[512]{};
 if(radio_running.load()){
#ifdef VIBE_RECEIVER
  wifi_sta_list_t stations{};esp_err_t error=esp_wifi_ap_get_sta_list(&stations);
  uint8_t mac[6]{},channel=0;wifi_second_chan_t secondary{};
  esp_wifi_get_mac(WIFI_IF_AP,mac);esp_wifi_get_channel(&channel,&secondary);
  snprintf(wifi,sizeof(wifi)," ap_stations=%d wifi_query=0x%x ap_mac=%02x%02x%02x%02x%02x%02x ch=%u",error==ESP_OK?int(stations.num):-1,unsigned(error),mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],unsigned(channel));
#else
  wifi_ap_record_t ap{};esp_err_t error=esp_wifi_sta_get_ap_info(&ap);
  esp_netif_ip_info_t ip{};esp_netif_get_ip_info(radio_netif,&ip);
  esp_netif_dhcp_status_t dhcp{};esp_netif_dhcpc_get_status(radio_netif,&dhcp);
  snprintf(wifi,sizeof(wifi)," wifi_up=%d associated=%d rssi=%d wifi_query=0x%x net_up=%d ip=%08lx dhcp=%d wifi_attempts=%u wifi_disc=%u wifi_reason=%d wifi_connect_err=0x%x wifi_timeouts=%u tcp_attempts=%u tcp_errno=%d tcp_fail=%u/%u tcp_recover=%u tcp_disc_err=0x%x bssid=%02x%02x%02x%02x%02x%02x ch=%u",
   int(wifi_up.load()),int(error==ESP_OK),error==ESP_OK?int(ap.rssi):0,unsigned(error),int(esp_netif_is_netif_up(radio_netif)),ip.ip.addr,int(dhcp),
   unsigned(wifi_attempts.load()),unsigned(wifi_disconnects.load()),wifi_reason.load(),unsigned(wifi_connect_error.load()),unsigned(wifi_timeouts.load()),unsigned(tcp_attempts.load()),tcp_error.load(),
   unsigned(tcp_failure_run.load()),unsigned(tcp_failure_ms.load()),unsigned(tcp_recoveries.load()),unsigned(tcp_disconnect_error.load()),ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5],unsigned(ap.primary));
#endif
 }
 snprintf(out,size,"radio=%d host=%d sessions=%u audio_tx=%u audio_rx=%u tx_err=%u lost=%u over=%u under=%u buffered=%u remote_mv=%u remote_level=%u remote_listen=%u radio_init=%d radio_err=0x%x radio_stage=%s radio_errno=%d radio_stack_min=%u%s",
  int(link_alive()),int(view.host),unsigned(connections.load()),unsigned(tx_audio_count.load()),unsigned(rx_audio_count.load()),unsigned(tx_errors.load()),lost,over,under,buffered,unsigned(p.battery_mv),unsigned(p.level),unsigned(p.listening),int(view.initialized),unsigned(view.error),view.stage,radio_errno.load(),unsigned(init_stack_min.load()==UINT32_MAX?0:init_stack_min.load()),wifi);
}
// Workers cannot see partially constructed queues or network state.
static bool wait_for_radio(){
 while(!radio_running.load() && radio_error.load()==ESP_OK)vTaskDelay(pdMS_TO_TICKS(10));
 return radio_running.load();
}
static void control_worker(void*){
 if(wait_for_radio()){
#ifdef VIBE_RECEIVER
  receiver_control(nullptr);
#else
  sender_control(nullptr);
#endif
 }
 vTaskDelete(nullptr);
}
static void audio_worker(void*){
 if(wait_for_radio()){
#ifdef VIBE_RECEIVER
  receiver_audio(nullptr);
#else
  sender_audio(nullptr);
#endif
 }
 vTaskDelete(nullptr);
}
// ESP-IDF reports this high-water mark in bytes. This helper is called only
// from initialize_radio(), so USB/status never samples the wrong task's stack.
static void sample_init_stack(){
 uint32_t available=uxTaskGetStackHighWaterMark(nullptr),previous=init_stack_min.load();
 while(available<previous && !init_stack_min.compare_exchange_weak(previous,available)){}
}
// Default create_wifi helpers assert internally, so use their checked primitives.
static esp_err_t initialize_radio(){
 sample_init_stack();
 esp_netif_t *netif=nullptr;bool wifi_initialized=false,driver_attached=false;
 esp_event_handler_instance_t wifi_handler=nullptr,ip_handler=nullptr;
 auto fail=[&](esp_err_t error){
  sample_init_stack();
  radio_fail(radio_stage.load(),error);
  // Once allocator metadata is damaged, freeing it can panic before USB can
  // report the diagnostic. Keep resources allocated until an explicit restart.
  if(strncmp(radio_stage.load(),"heap_",5)==0)return error;
  if(wifi_handler)esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler);
  if(ip_handler)esp_event_handler_instance_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,ip_handler);
  if(wifi_initialized){esp_wifi_stop();esp_wifi_deinit();}
  if(netif){
   if(driver_attached)esp_wifi_clear_default_wifi_driver_and_handlers(netif);
   esp_netif_destroy(netif);
  }
  // Queues are never visible to USB until radio_running is published.
#ifdef VIBE_RECEIVER
  if(keys_out){vQueueDelete(keys_out);keys_out=nullptr;}
  if(wheels_out){vQueueDelete(wheels_out);wheels_out=nullptr;}
#else
  if(controls){vQueueDelete(controls);controls=nullptr;}
  if(audios){vQueueDelete(audios);audios=nullptr;}
#endif
  return error;
 };
#define RADIO_CHECK(stage,call) do{radio_stage=stage;sample_init_stack();esp_err_t checked=(call);sample_init_stack();if(checked!=ESP_OK)return fail(checked);}while(0)
 radio_stage="queues";
#ifdef VIBE_RECEIVER
 keys_out=xQueueCreate(48,sizeof(KeyEvent));wheels_out=xQueueCreate(32,sizeof(WheelEvent));
 if(!keys_out || !wheels_out)return fail(ESP_ERR_NO_MEM);
#else
 controls=xQueueCreate(48,sizeof(Control));audios=xQueueCreate(6,sizeof(Audio));
 if(!controls || !audios)return fail(ESP_ERR_NO_MEM);
#endif
 RADIO_CHECK("heap_before_nvs",heap_caps_check_integrity_all(true)?ESP_OK:ESP_ERR_INVALID_STATE);
 RADIO_CHECK("nvs",nvs_flash_init()); // Never erase existing NVS implicitly.
 RADIO_CHECK("heap_after_nvs",heap_caps_check_integrity_all(true)?ESP_OK:ESP_ERR_INVALID_STATE);
 RADIO_CHECK("netif",esp_netif_init());
 radio_stage="event_loop";
 esp_err_t err=esp_event_loop_create_default();
 if(err!=ESP_OK && err!=ESP_ERR_INVALID_STATE)return fail(err); // Existing default loop is usable.
 radio_stage="netif_create";
#ifdef VIBE_RECEIVER
 esp_netif_config_t netif_config=ESP_NETIF_DEFAULT_WIFI_AP();
#else
 esp_netif_config_t netif_config=ESP_NETIF_DEFAULT_WIFI_STA();
#endif
 netif=esp_netif_new(&netif_config);if(!netif)return fail(ESP_ERR_NO_MEM);
 radio_netif=netif;
 // Mark before attach: IDF records the netif even when driver allocation fails.
 driver_attached=true;
#ifdef VIBE_RECEIVER
 RADIO_CHECK("netif_attach",esp_netif_attach_wifi_ap(netif));
 RADIO_CHECK("wifi_handlers",esp_wifi_set_default_wifi_ap_handlers());
#else
 RADIO_CHECK("netif_attach",esp_netif_attach_wifi_station(netif));
 RADIO_CHECK("wifi_handlers",esp_wifi_set_default_wifi_sta_handlers());
#endif
 esp_netif_ip_info_t ip{};IP4_ADDR(&ip.ip,192,168,7,
#ifdef VIBE_RECEIVER
 1
#else
 2
#endif
 );IP4_ADDR(&ip.gw,192,168,7,1);IP4_ADDR(&ip.netmask,255,255,255,0);
 radio_stage="dhcp_stop";
#ifdef VIBE_RECEIVER
 err=esp_netif_dhcps_stop(netif);
#else
 err=esp_netif_dhcpc_stop(netif);
#endif
 if(err!=ESP_OK && err!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)return fail(err);
 RADIO_CHECK("static_ip",esp_netif_set_ip_info(netif,&ip));
 wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
 RADIO_CHECK("heap_before_wifi",heap_caps_check_integrity_all(true)?ESP_OK:ESP_ERR_INVALID_STATE);
 RADIO_CHECK("wifi_init",esp_wifi_init(&init));wifi_initialized=true;
 RADIO_CHECK("heap_after_wifi",heap_caps_check_integrity_all(true)?ESP_OK:ESP_ERR_INVALID_STATE);
 RADIO_CHECK("wifi_storage",esp_wifi_set_storage(WIFI_STORAGE_RAM));
 RADIO_CHECK("wifi_events",esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,nullptr,&wifi_handler));
 RADIO_CHECK("ip_events",esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,nullptr,&ip_handler));
 wifi_config_t config{};
#ifdef VIBE_RECEIVER
 strcpy((char*)config.ap.ssid,VIBE_WIFI_SSID);strcpy((char*)config.ap.password,VIBE_WIFI_PASSWORD);config.ap.ssid_len=strlen(VIBE_WIFI_SSID);config.ap.channel=6;config.ap.max_connection=1;config.ap.authmode=WIFI_AUTH_WPA2_PSK;config.ap.pmf_cfg.capable=true;
 RADIO_CHECK("wifi_mode",esp_wifi_set_mode(WIFI_MODE_AP));RADIO_CHECK("wifi_config",esp_wifi_set_config(WIFI_IF_AP,&config));
#else
 strcpy((char*)config.sta.ssid,VIBE_WIFI_SSID);strcpy((char*)config.sta.password,VIBE_WIFI_PASSWORD);config.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
 RADIO_CHECK("wifi_mode",esp_wifi_set_mode(WIFI_MODE_STA));RADIO_CHECK("wifi_config",esp_wifi_set_config(WIFI_IF_STA,&config));
#endif
 RADIO_CHECK("wifi_start",esp_wifi_start());RADIO_CHECK("wifi_ps",esp_wifi_set_ps(WIFI_PS_NONE));
 radio_stage="control_task";
 if(xTaskCreate(control_worker,"radio_control",6144,nullptr,5,nullptr)!=pdPASS)return fail(ESP_ERR_NO_MEM);
 radio_stage="audio_task";
 if(xTaskCreate(audio_worker,"radio_audio",6144,nullptr,5,nullptr)!=pdPASS)return fail(ESP_ERR_NO_MEM);
 sample_init_stack();radio_stage="ready";radio_running=true;
 return ESP_OK;
#undef RADIO_CHECK
}
static void initialization_worker(void*){initialize_radio();vTaskDelete(nullptr);}
extern "C" esp_err_t wireless_init(){
 if(init_requested.exchange(true))return ESP_ERR_INVALID_STATE;
 radio_stage="init_task";
 if(xTaskCreate(initialization_worker,"radio_init",6144,nullptr,3,nullptr)!=pdPASS)return radio_fail("init_task",ESP_ERR_NO_MEM);
 return ESP_OK;
}
