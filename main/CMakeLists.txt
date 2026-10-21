idf_component_register(SRCS "main.cpp" "usb.c" "wireless.cpp" "power_history.cpp" INCLUDE_DIRS "." "../private" REQUIRES M5Unified tinyusb nvs_flash esp_timer esp_pm esp_partition driver usb esp_wifi esp_netif lwip EMBED_FILES
 "assets/frog_happy.png" "assets/frog_laugh.png" "assets/frog_surprised.png" "assets/frog_shy.png"
 "assets/frog_sad.png" "assets/frog_angry.png" "assets/frog_sleepy.png" "assets/frog_wink.png")
idf_component_get_property(tusb_lib tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}")
option(VIBE_RED_DOWNLOAD_LOCK "Lock PMIC red-button download; unlocking requires full PMIC power removal" OFF)
target_compile_definitions(${COMPONENT_LIB} PRIVATE VIBE_RED_DOWNLOAD_LOCK=$<BOOL:${VIBE_RED_DOWNLOAD_LOCK}>)
if(DEFINED VIBE_DISPLAY_IDLE_MS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE VIBE_DISPLAY_IDLE_MS=${VIBE_DISPLAY_IDLE_MS})
endif()
