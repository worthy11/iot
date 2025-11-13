#include <stdio.h>
#include <stdlib.h>

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_console.h"

#include "driver/uart.h"
#include "gatt_client.h"
#include "manager.h"

void gatt_client_main(void)
{
    register_console_commands();
    init_ble_manager();
    return;
}