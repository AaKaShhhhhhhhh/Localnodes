idf_component_register(SRCS "main.c" "network_state.c"
                       INCLUDE_DIRS "."
                       REQUIRES flash_queue esp_wifi esp_event nvs_flash)