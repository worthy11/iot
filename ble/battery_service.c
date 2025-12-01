#include "battery_service.h"
#include "common.h"
#include "host/ble_gatt.h"
#include "esp_log.h"

static const char *TAG = "Battery_Service";

/* Battery Service UUID */
static const ble_uuid16_t battery_svc_uuid = BLE_UUID16_INIT(0x180F);

/* Battery Level Characteristic UUID */
static const ble_uuid16_t battery_chr_uuid = BLE_UUID16_INIT(0x2A19);

/* Characteristic value handle */
static uint16_t battery_chr_val_handle;

/* Battery level value */
static uint8_t battery_level = 100;

/* Forward declarations */
static int battery_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Service definition */
static struct ble_gatt_svc_def battery_svc_def[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &battery_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = &battery_chr_uuid.u,
              .access_cb = battery_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &battery_chr_val_handle},
             {0}}},
    {0}};

/* Characteristic access callback */
static int battery_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Battery Level read; conn_handle=%d level=%d%%", conn_handle, battery_level);
        rc = os_mbuf_append(ctxt->om, &battery_level, sizeof(battery_level));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    default:
        ESP_LOGE(TAG, "unexpected access operation to Battery Level, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* Public functions */
const struct ble_gatt_svc_def *battery_service_get_svc_def(void)
{
    return battery_svc_def;
}

int battery_service_init(void)
{
    battery_level = 100;
    return 0;
}

void battery_service_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "registering characteristic %s with def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;
    default:
        break;
    }
}

void battery_service_subscribe_cb(struct ble_gap_event *event)
{
    /* Battery service doesn't need subscription handling for now */
    (void)event;
}

uint8_t get_battery_level(void)
{
    return battery_level;
}

void update_battery_level(void)
{
    battery_level = battery_level > 0 ? battery_level - 0.1 : 100;
}
