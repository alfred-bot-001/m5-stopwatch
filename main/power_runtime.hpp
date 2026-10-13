#pragma once
#include "esp_pm.h"

// This object and its lock have one owner: app_main. The RTOS also requests the
// maximum clock while tasks are runnable; 80 MHz is the idle floor, not a claim
// that every non-speaking instruction executes at that clock.
class PowerRuntime {
public:
 bool begin() {
  if(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"vibe_interact",&lock_)!=ESP_OK)return false;
  esp_pm_config_t config{};
  config.max_freq_mhz=240;config.min_freq_mhz=80;config.light_sleep_enable=false;
  if(esp_pm_configure(&config)!=ESP_OK){esp_pm_lock_delete(lock_);lock_=nullptr;return false;}
  return true;
 }
 bool boost(bool enable) {
  if(!lock_)return false;
  if(enable==held_)return true;
  const auto err=enable?esp_pm_lock_acquire(lock_):esp_pm_lock_release(lock_);
  if(err!=ESP_OK)return false;
  held_=enable;return true;
 }
 bool held() const {return held_;}
private:
 esp_pm_lock_handle_t lock_=nullptr;
 bool held_=false;
};
