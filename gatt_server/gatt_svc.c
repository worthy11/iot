
#include "gatt_svc.h"
#include "common.h"
#include "battery_service.h"
#include "led_service.h"
#include "keyboard_service.h"

static const char *TAG = "gatt_svc";


static int battery_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);

static const ble_uuid16_t battery_svc_uuid = BLE_UUID16_INIT(0x180F);

static uint16_t battery_chr_val_handle;
static const ble_uuid16_t battery_chr_uuid = BLE_UUID16_INIT(0x2A19);


static uint16_t battery_chr_conn_handle = 0;
static bool battery_chr_conn_handle_inited = false;
static bool battery_ind_status = false;

static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

static const ble_uuid16_t led_svc_uuid = BLE_UUID16_INIT(0x1802);  
static const ble_uuid16_t led_chr_uuid = BLE_UUID16_INIT(0x2A06);  

static uint16_t led_chr_val_handle;


static const struct ble_gatt_svc_def gatt_svr_svcs[] = {

    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &battery_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {
              .uuid = &battery_chr_uuid.u,
              .access_cb = battery_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE,
              .val_handle = &battery_chr_val_handle},
             {
                 0, 
             }}},
    {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &led_svc_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
        {
            .uuid = &led_chr_uuid.u,
            .access_cb = led_chr_access,
            .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            .val_handle = &led_chr_val_handle
        },
        { 0 } 
    }
},

{
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &keyboard_svc_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
        {
            .uuid = &keyboard_chr_uuid.u,
            .access_cb = keyboard_chr_access,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &keyboard_chr_val_handle
        },
        { 0 }   
    }
},
    
    
    {
        0,
    },
};

static int battery_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    
    int rc;

    switch (ctxt->op) {

    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "characteristic read; conn_handle=%d attr_handle=%d",
                     conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "characteristic read by nimble stack; attr_handle=%d",
                     attr_handle);
        }

        if (attr_handle == battery_chr_val_handle) {
            uint8_t level = get_battery_level();
            rc = os_mbuf_append(ctxt->om, &level, 1);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        goto error;

    default:
        goto error;
    }

error:
    ESP_LOGE(
        TAG,
        "unexpected access operation to battery characteristic, opcode: %d",
        ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}


static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    int rc;

    switch (ctxt->op) {

    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "characteristic read; conn_handle=%d attr_handle=%d",
                     conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "characteristic read by nimble stack; attr_handle=%d",
                     attr_handle);
        }

        if (attr_handle == led_chr_val_handle) {
            uint8_t state = get_led_state();
            rc = os_mbuf_append(ctxt->om, &state, 1);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        goto error;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "characteristic write; conn_handle=%d attr_handle=%d",
                     conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG,
                     "characteristic write by nimble stack; attr_handle=%d",
                     attr_handle);
        }

        if (attr_handle == led_chr_val_handle) {
            uint8_t val = 0;
            rc = os_mbuf_copydata(ctxt->om, 0, 1, &val);
            if (rc != 0) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            if (val) {
                led_on();
                ESP_LOGI(TAG, "led turned on!");
            } else {
                led_off();
                ESP_LOGI(TAG, "led turned off!");
            }
            return 0;
        }
        goto error;

    default:
        goto error;
    }

error:
    ESP_LOGE(TAG,
             "unexpected access operation to led characteristic, opcode: %d",
             ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}



void send_battery_level_indication(void) {
    if (battery_ind_status && battery_chr_conn_handle_inited) {
        ble_gatts_indicate(battery_chr_conn_handle,
                           battery_chr_val_handle);
        ESP_LOGI(TAG, "battery level indication sent!");
    }
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {

    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG,
                 "registering characteristic %s with "
                 "def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "registering descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}


void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
   
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
    } else {
        ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d",
                 event->subscribe.attr_handle);
    }

    if (event->subscribe.attr_handle == battery_chr_val_handle) {
        battery_chr_conn_handle = event->subscribe.conn_handle;
        battery_chr_conn_handle_inited = true;
        battery_ind_status = event->subscribe.cur_indicate;
    }   

    if (event->subscribe.attr_handle == keyboard_chr_val_handle) {
        keyboard_conn_handle = event->subscribe.conn_handle;
        keyboard_notify_enabled = event->subscribe.cur_notify;
    }
}


int gatt_svc_init(void) {
   
    int rc;

    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}