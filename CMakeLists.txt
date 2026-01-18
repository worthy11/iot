idf_component_register(SRCS "main.c"
                           "event_manager.c"
                           
                           "wifi/wifi_manager.c"
                           
                           "ble/gap.c"
                           "ble/gatt_server.c"
                           "ble/gatt_svc.c"
                           "ble/provisioning_service.c"
                           "ble/telemetry_service.c"
                           "ble/command_service.c"
                           "ble/ble_manager.c"
                           "utils/nvs_utils.c"
                           "utils/fs_utils.c"
                           
                           "mqtt/mqtt_manager.c"
                           "mqtt/http_manager.c"
                           
                           "hardware/buttons/button.c"
                           "hardware/buttons/left_button.c"
                           "hardware/buttons/confirm_button.c"
                           "hardware/buttons/right_button.c"
                           "hardware/display/display_driver.c"
                           "hardware/display/ssd1306.c"
                           "hardware/feeder/beam_driver.c"
                           "hardware/feeder/motor_driver.c"
                           "hardware/ph/ph_sensor_driver.c"
                           "hardware/temperature/temp_sensor_driver.c"
                           "hardware/hardware_manager.c"

                    INCLUDE_DIRS "." "wifi" "ble" "utils" "hardware" "mqtt"
                    REQUIRES esp_wifi console nvs_flash driver bt mqtt esp_adc json spiffs esp_https_ota esp_http_client app_update)