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
                           "hardware/beam_driver.c"
                           "hardware/button_driver.c"
                           "hardware/motor_driver.c"
                           "hardware/ssd1306.c"
                           "hardware/ssd1306_demo.c"
                           "hardware/ph_sensor_driver.c"
                           "hardware/temp_sensor_driver.c"
                           
                    INCLUDE_DIRS "." "wifi" "ble" "hardware" "mqtt"
                    REQUIRES esp_wifi console nvs_flash driver bt mqtt)