idf_component_register(SRCS "main.c" "sk9822.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server mqtt nvs_flash esp_wifi tcpip_adapter esp_event)
