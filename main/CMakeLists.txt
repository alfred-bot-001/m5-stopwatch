idf_component_register(SRCS "main.cpp" "usb.c" "wireless.cpp" INCLUDE_DIRS "." "../private" REQUIRES M5Unified tinyusb nvs_flash esp_timer driver usb esp_wifi esp_netif lwip)
idf_component_get_property(tusb_lib tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}")
