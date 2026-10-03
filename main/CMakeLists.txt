idf_component_register(SRCS "main.cpp" "usb.c" INCLUDE_DIRS "." REQUIRES M5Unified tinyusb nvs_flash esp_timer driver usb)
idf_component_get_property(tusb_lib tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}")
