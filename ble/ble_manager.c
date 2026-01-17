#include "ble_manager.h"
#include "esp_log.h"
#include "gatt_server.h"
#include "telemetry_service.h"
#include "gap.h"

static const char *TAG = "ble_manager";

void ble_start_advertising(void)
{
    start_gatt_server();
}

void ble_stop_advertising(void)
{
    stop_gatt_server();
}

uint32_t ble_manager_get_passkey(void)
{
    return gap_get_current_passkey();
}

void ble_manager_init(void)
{
    gatt_server_init();
}

void ble_manager_notify_temperature(float temperature)
{
    telemetry_service_notify_temperature(temperature);
}

void ble_manager_notify_ph(float ph)
{
    telemetry_service_notify_ph(ph);
}

void ble_manager_notify_feed(bool success)
{
    telemetry_service_notify_feed(success);
}