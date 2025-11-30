idf_component_register(SRCS "main.c"
                           "gatt_server/battery_service.c"
                           "gatt_server/device_info_service.c"
                           "gatt_server/gap.c"
                           "gatt_server/gatt_server.c"
                           "gatt_server/gatt_svc.c"
                           "gatt_server/wifi_config_service.c"
                           
                           "hardware/beam_driver.c"
                           "hardware/button_driver.c"
                           "hardware/motor_driver.c"
                           "hardware/oled_manager.c"
                           "hardware/ph_sensor_driver.c"
                           "hardware/temp_sensor_driver.c"
                           
                           "mqtt/mqtt_publisher.c"
                           
                           "hardware_manager.c"
                           "wifi_manager.c"
                    INCLUDE_DIRS "." "gatt_server" "hardware" "mqtt"
                    REQUIRES esp_wifi console nvs_flash driver bt mqtt)