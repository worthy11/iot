idf_component_register(SRCS "main.c"
                           "event_manager.c"
                           
                           "wifi/wifi_manager.c"
                           
                           "ble/battery_service.c"
                           "ble/device_info_service.c"
                           "ble/gap.c"
                           "ble/gatt_server.c"
                           "ble/gatt_svc.c"
                           "ble/wifi_config_service.c"
                           
                           "mqtt/mqtt_publisher.c"

                           "hardware/hardware_manager.c"
                           "hardware/buttons/button.c"
                           "hardware/buttons/config_button.c"
                           "hardware/buttons/feed_button.c"
                           "hardware/buttons/display_button.c"
                           "hardware/display/display_manager.c"
                           "hardware/display/ssd1306.c"
                           "hardware/feeder/beam_driver.c"
                           "hardware/feeder/motor_driver.c"
                           "hardware/ph/ph_sensor_driver.c"
                           "hardware/temperature/temp_sensor_driver.c"
                           
                    INCLUDE_DIRS "." "wifi" "ble" "hardware" "mqtt"
                    REQUIRES esp_wifi console nvs_flash driver bt mqtt esp_adc)