#include "device_info_service.h"
#include "common.h"
#include "host/ble_gatt.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "Device_Info_Service";

/* Device Information Service UUID */
static const ble_uuid16_t device_info_svc_uuid = BLE_UUID16_INIT(0x180A);

/* Characteristic UUIDs */
static const ble_uuid16_t manufacturer_name_chr_uuid = BLE_UUID16_INIT(0x2A29);
static const ble_uuid16_t model_number_chr_uuid = BLE_UUID16_INIT(0x2A24);
static const ble_uuid16_t serial_number_chr_uuid = BLE_UUID16_INIT(0x2A25);
static const ble_uuid16_t firmware_revision_chr_uuid = BLE_UUID16_INIT(0x2A26);
static const ble_uuid16_t software_revision_chr_uuid = BLE_UUID16_INIT(0x2A28);
static const ble_uuid16_t pnp_id_chr_uuid = BLE_UUID16_INIT(0x2A50);

/* Characteristic values */
static const char *manufacturer_name = "Logitech";
static const char *model_number = "POP Icon Keys";
static const char *serial_number = "5E02E892";
static const char *firmware_revision = "RBK95.00_0007";
static const char *software_revision = "00590A0126";

/* PnP ID: Vendor ID Source (0x02 = USB Implementer's Forum),
   Vendor ID (0x046D = 1133), Product ID (0xB38F = 45967), Product Version (0x0007 = 7) */
static const uint8_t pnp_id[] = {
    0x02,       // Vendor ID Source: USB Implementer's Forum
    0x6D, 0x04, // Vendor ID: 0x046D (1133) - little-endian
    0x8F, 0xB3, // Product ID: 0xB38F (45967) - little-endian
    0x07, 0x00  // Product Version: 0x0007 (7) - little-endian
};

/* Forward declarations */
static int device_info_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Service definition */
static struct ble_gatt_svc_def device_info_svc_def[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &device_info_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = &manufacturer_name_chr_uuid.u,
              .access_cb = device_info_chr_access,
              .flags = BLE_GATT_CHR_F_READ},
             {.uuid = &model_number_chr_uuid.u,
              .access_cb = device_info_chr_access,
              .flags = BLE_GATT_CHR_F_READ},
             {.uuid = &serial_number_chr_uuid.u,
              .access_cb = device_info_chr_access,
              .flags = BLE_GATT_CHR_F_READ},
             {.uuid = &firmware_revision_chr_uuid.u,
              .access_cb = device_info_chr_access,
              .flags = BLE_GATT_CHR_F_READ},
             {.uuid = &software_revision_chr_uuid.u,
              .access_cb = device_info_chr_access,
              .flags = BLE_GATT_CHR_F_READ},
             {.uuid = &pnp_id_chr_uuid.u,
              .access_cb = device_info_chr_access,
              .flags = BLE_GATT_CHR_F_READ},
             {0}}},
    {0}};

/* Characteristic access callback */
static int device_info_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    const void *data = NULL;
    size_t data_len = 0;
    uint16_t uuid16;

    (void)arg; /* Not used - we identify by UUID */

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        uuid16 = ble_uuid_u16(ctxt->chr->uuid);

        switch (uuid16)
        {
        case 0x2A29: /* Manufacturer Name String */
            data = manufacturer_name;
            data_len = strlen(manufacturer_name);
            ESP_LOGI(TAG, "Manufacturer Name read; conn_handle=%d value=%s", conn_handle, manufacturer_name);
            break;
        case 0x2A24: /* Model Number String */
            data = model_number;
            data_len = strlen(model_number);
            ESP_LOGI(TAG, "Model Number read; conn_handle=%d value=%s", conn_handle, model_number);
            break;
        case 0x2A25: /* Serial Number String */
            data = serial_number;
            data_len = strlen(serial_number);
            ESP_LOGI(TAG, "Serial Number read; conn_handle=%d value=%s", conn_handle, serial_number);
            break;
        case 0x2A26: /* Firmware Revision String */
            data = firmware_revision;
            data_len = strlen(firmware_revision);
            ESP_LOGI(TAG, "Firmware Revision read; conn_handle=%d value=%s", conn_handle, firmware_revision);
            break;
        case 0x2A28: /* Software Revision String */
            data = software_revision;
            data_len = strlen(software_revision);
            ESP_LOGI(TAG, "Software Revision read; conn_handle=%d value=%s", conn_handle, software_revision);
            break;
        case 0x2A50: /* PnP ID */
            data = pnp_id;
            data_len = sizeof(pnp_id);
            ESP_LOGI(TAG, "PnP ID read; conn_handle=%d", conn_handle);
            break;
        default:
            ESP_LOGE(TAG, "Unknown Device Info characteristic UUID: 0x%04X", uuid16);
            return BLE_ATT_ERR_UNLIKELY;
        }

        if (data == NULL || data_len == 0)
        {
            ESP_LOGE(TAG, "Device Info characteristic data is NULL or empty");
            return BLE_ATT_ERR_UNLIKELY;
        }

        rc = os_mbuf_append(ctxt->om, data, data_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    default:
        ESP_LOGE(TAG, "unexpected access operation to Device Info characteristic, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* Public functions */
const struct ble_gatt_svc_def *device_info_service_get_svc_def(void)
{
    return device_info_svc_def;
}

int device_info_service_init(void)
{
    return 0;
}

void device_info_service_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    uint16_t uuid16;

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        /* Only log if this is the Device Information Service (0x180A) */
        uuid16 = ble_uuid_u16(ctxt->svc.svc_def->uuid);
        if (uuid16 == 0x180A)
        {
            ESP_LOGI(TAG, "registered Device Information Service %s with handle=%d",
                     ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                     ctxt->svc.handle);
        }
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        /* Only log if this characteristic belongs to Device Information Service */
        /* Check if it's one of our Device Info characteristics */
        uuid16 = ble_uuid_u16(ctxt->chr.chr_def->uuid);
        if (uuid16 == 0x2A29 || uuid16 == 0x2A24 || uuid16 == 0x2A25 ||
            uuid16 == 0x2A26 || uuid16 == 0x2A28 || uuid16 == 0x2A50)
        {
            ESP_LOGI(TAG, "registering Device Info characteristic %s with def_handle=%d val_handle=%d",
                     ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                     ctxt->chr.def_handle, ctxt->chr.val_handle);
        }
        break;
    default:
        break;
    }
}

void device_info_service_subscribe_cb(struct ble_gap_event *event)
{
    /* Device Information Service doesn't support subscriptions */
    (void)event;
}
