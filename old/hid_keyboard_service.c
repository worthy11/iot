#include "hid_keyboard_service.h"
#include "common.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#undef TAG
static const char *TAG = "HID_Keyboard_Service";

/* HID Service UUID */
static const ble_uuid16_t hid_svc_uuid = BLE_UUID16_INIT(0x1812);

/* Vendor Service UUID */
static const ble_uuid16_t vendor_svc_uuid = BLE_UUID16_INIT(0xFD72);

/* Vendor Characteristic UUIDs (128-bit vendor-specific) */
static const ble_uuid128_t vendor_chr1_uuid = BLE_UUID128_INIT(0x00, 0x00, 0x72, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x80, 0x00, 0x9B, 0x5F);
static const ble_uuid128_t vendor_chr2_uuid = BLE_UUID128_INIT(0x01, 0x00, 0x72, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x80, 0x00, 0x9B, 0x5F);
static const ble_uuid128_t vendor_chr3_uuid = BLE_UUID128_INIT(0x02, 0x00, 0x72, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x80, 0x00, 0x9B, 0x5F);
static const ble_uuid128_t vendor_chr4_uuid = BLE_UUID128_INIT(0x03, 0x00, 0x72, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x80, 0x00, 0x9B, 0x5F);
static const ble_uuid128_t vendor_chr5_uuid = BLE_UUID128_INIT(0x04, 0x00, 0x72, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x80, 0x00, 0x9B, 0x5F);
static const ble_uuid128_t vendor_chr6_uuid = BLE_UUID128_INIT(0x05, 0x00, 0x72, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x80, 0x00, 0x9B, 0x5F);

/* HID Characteristic UUIDs */
static const ble_uuid16_t hid_info_chr_uuid = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t hid_report_map_chr_uuid = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t hid_control_point_chr_uuid = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t hid_protocol_mode_chr_uuid = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t boot_kbd_input_chr_uuid = BLE_UUID16_INIT(0x2A22);
static const ble_uuid16_t boot_kbd_output_chr_uuid = BLE_UUID16_INIT(0x2A32);
static const ble_uuid16_t hid_report_chr_uuid = BLE_UUID16_INIT(0x2A4D);

/* Report Reference Descriptor UUID */
static const ble_uuid16_t report_ref_uuid = BLE_UUID16_INIT(0x2908);

/* Report Reference Descriptor values */
static const uint8_t boot_kbd_input_report_ref[2] = {0x01, 0x01};
static const uint8_t boot_kbd_output_report_ref[2] = {0x01, 0x02};
static const uint8_t hid_report_ref[2] = {0x01, 0x01};

/* HID Information characteristic value */
static const uint8_t hid_info_val[4] = {0x11, 0x01, 0x00, 0x03};

/* HID Report Map */
static const uint8_t hid_report_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0x05, 0x08,
    0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0xc0};

/* Characteristic value handles */
static uint16_t hid_info_chr_val_handle;
static uint16_t hid_report_map_chr_val_handle;
static uint16_t hid_control_point_chr_val_handle;
static uint16_t hid_protocol_mode_chr_val_handle;
static uint16_t boot_kbd_input_chr_val_handle;
static uint16_t boot_kbd_output_chr_val_handle;
static uint16_t hid_report_chr_val_handle;

/* Vendor characteristic value handles */
static uint16_t vendor_chr1_val_handle;
static uint16_t vendor_chr2_val_handle;
static uint16_t vendor_chr3_val_handle;
static uint16_t vendor_chr4_val_handle;
static uint16_t vendor_chr5_val_handle;
static uint16_t vendor_chr6_val_handle;

/* Vendor characteristic values */
static uint8_t vendor_chr1_val[1] = {0};
static uint8_t vendor_chr2_val[1] = {0};
static uint8_t vendor_chr3_val[1] = {0};
static uint8_t vendor_chr4_val[1] = {0};
static uint8_t vendor_chr5_val[1] = {0};
static uint8_t vendor_chr6_val[1] = {0};

/* Protocol mode: 0 = Boot Protocol, 1 = Report Protocol */
static uint8_t protocol_mode = 1;

/* Boot Keyboard Input Report state */
static uint8_t boot_kbd_input_report[8] = {0};

/* Boot Keyboard Output Report state */
static uint8_t boot_kbd_output_report = 0;

/* Connection handles for notifications */
static uint16_t kbd_input_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool kbd_input_notify_enabled = false;
static uint16_t kbd_report_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool kbd_report_notify_enabled = false;

/* Task handle for keyboard auto-send task */
static TaskHandle_t keyboard_auto_send_task_handle = NULL;

/* Forward declarations */
static int hid_info_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_report_map_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_control_point_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_protocol_mode_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg);
static int boot_kbd_input_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg);
static int boot_kbd_output_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_report_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int report_ref_dsc_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int vendor_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static void keyboard_auto_send_task(void *param);

/* Service definition */
static struct ble_gatt_svc_def hid_keyboard_svc_def[] = {
    /* HID Service */
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &hid_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = &hid_info_chr_uuid.u,
              .access_cb = hid_info_chr_access,
              .flags = BLE_GATT_CHR_F_READ,
              .val_handle = &hid_info_chr_val_handle},
             {.uuid = &hid_report_map_chr_uuid.u,
              .access_cb = hid_report_map_chr_access,
              .flags = BLE_GATT_CHR_F_READ,
              .val_handle = &hid_report_map_chr_val_handle},
             {.uuid = &hid_control_point_chr_uuid.u,
              .access_cb = hid_control_point_chr_access,
              .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
              .val_handle = &hid_control_point_chr_val_handle},
             {.uuid = &hid_protocol_mode_chr_uuid.u,
              .access_cb = hid_protocol_mode_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
              .val_handle = &hid_protocol_mode_chr_val_handle},
             {.uuid = &boot_kbd_input_chr_uuid.u,
              .access_cb = boot_kbd_input_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &boot_kbd_input_chr_val_handle,
              .descriptors =
                  (struct ble_gatt_dsc_def[]){
                      {.uuid = &report_ref_uuid.u,
                       .access_cb = report_ref_dsc_access,
                       .att_flags = BLE_ATT_F_READ,
                       .arg = (void *)boot_kbd_input_report_ref},
                      {0}}},
             {.uuid = &boot_kbd_output_chr_uuid.u,
              .access_cb = boot_kbd_output_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
              .val_handle = &boot_kbd_output_chr_val_handle,
              .descriptors =
                  (struct ble_gatt_dsc_def[]){
                      {.uuid = &report_ref_uuid.u,
                       .access_cb = report_ref_dsc_access,
                       .att_flags = BLE_ATT_F_READ,
                       .arg = (void *)boot_kbd_output_report_ref},
                      {0}}},
             {.uuid = &hid_report_chr_uuid.u,
              .access_cb = hid_report_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &hid_report_chr_val_handle,
              .descriptors =
                  (struct ble_gatt_dsc_def[]){
                      {.uuid = &report_ref_uuid.u,
                       .access_cb = report_ref_dsc_access,
                       .att_flags = BLE_ATT_F_READ,
                       .arg = (void *)hid_report_ref},
                      {0}}},
             {0}}},
    /* Vendor Service */
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &vendor_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = &vendor_chr1_uuid.u,
              .access_cb = vendor_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
              .val_handle = &vendor_chr1_val_handle},
             {.uuid = &vendor_chr2_uuid.u,
              .access_cb = vendor_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
              .val_handle = &vendor_chr2_val_handle},
             {.uuid = &vendor_chr3_uuid.u,
              .access_cb = vendor_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
              .val_handle = &vendor_chr3_val_handle},
             {.uuid = &vendor_chr4_uuid.u,
              .access_cb = vendor_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
              .val_handle = &vendor_chr4_val_handle},
             {.uuid = &vendor_chr5_uuid.u,
              .access_cb = vendor_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
              .val_handle = &vendor_chr5_val_handle},
             {.uuid = &vendor_chr6_uuid.u,
              .access_cb = vendor_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
              .val_handle = &vendor_chr6_val_handle},
             {0}}},
    {0}};

/* Characteristic access callbacks */
static int hid_info_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "HID Information read; conn_handle=%d", conn_handle);
        rc = os_mbuf_append(ctxt->om, hid_info_val, sizeof(hid_info_val));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    default:
        ESP_LOGE(TAG, "unexpected access operation to HID Information, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int hid_report_map_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Report Map read; conn_handle=%d", conn_handle);
        rc = os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    default:
        ESP_LOGE(TAG, "unexpected access operation to Report Map, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int hid_control_point_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (ctxt->om->om_len == 1)
        {
            uint8_t cmd = ctxt->om->om_data[0];
            ESP_LOGI(TAG, "HID Control Point write; conn_handle=%d cmd=0x%02X", conn_handle, cmd);
            if (cmd == 0x00)
            {
                ESP_LOGI(TAG, "Suspend command received");
            }
            else if (cmd == 0x01)
            {
                ESP_LOGI(TAG, "Exit Suspend command received");
            }
        }
        return 0;
    default:
        ESP_LOGE(TAG, "unexpected access operation to HID Control Point, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int hid_protocol_mode_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Protocol Mode read; conn_handle=%d mode=%d", conn_handle, protocol_mode);
        rc = os_mbuf_append(ctxt->om, &protocol_mode, sizeof(protocol_mode));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (ctxt->om->om_len == 1)
        {
            uint8_t mode = ctxt->om->om_data[0];
            if (mode <= 1)
            {
                protocol_mode = mode;
                ESP_LOGI(TAG, "Protocol Mode write; conn_handle=%d mode=%s (%d)",
                         conn_handle, mode == 0 ? "Boot Protocol" : "Report Protocol", mode);
            }
            else
            {
                ESP_LOGW(TAG, "Invalid Protocol Mode value: %d", mode);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
        }
        return 0;
    default:
        ESP_LOGE(TAG, "unexpected access operation to Protocol Mode, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int boot_kbd_input_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Boot Keyboard Input Report read; conn_handle=%d", conn_handle);
        rc = os_mbuf_append(ctxt->om, boot_kbd_input_report, sizeof(boot_kbd_input_report));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    default:
        ESP_LOGE(TAG, "unexpected access operation to Boot Keyboard Input Report, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int boot_kbd_output_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Boot Keyboard Output Report read; conn_handle=%d", conn_handle);
        return os_mbuf_append(ctxt->om, &boot_kbd_output_report, sizeof(boot_kbd_output_report)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (ctxt->om->om_len == 1)
        {
            boot_kbd_output_report = ctxt->om->om_data[0];
            ESP_LOGI(TAG, "Boot Keyboard Output Report write; conn_handle=%d LEDs=0x%02X",
                     conn_handle, boot_kbd_output_report);
        }
        return 0;
    default:
        ESP_LOGE(TAG, "unexpected access operation to Boot Keyboard Output Report, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int hid_report_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Report characteristic read; conn_handle=%d", conn_handle);
        rc = os_mbuf_append(ctxt->om, boot_kbd_input_report, sizeof(boot_kbd_input_report));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGI(TAG, "Report characteristic write; conn_handle=%d len=%d", conn_handle, ctxt->om->om_len);
        return 0;
    default:
        ESP_LOGE(TAG, "unexpected access operation to Report characteristic, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int report_ref_dsc_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    const uint8_t *report_ref = (const uint8_t *)arg;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_DSC:
        ESP_LOGI(TAG, "Report Reference Descriptor read; conn_handle=%d Report ID=0x%02X Type=0x%02X",
                 conn_handle, report_ref[0], report_ref[1]);
        rc = os_mbuf_append(ctxt->om, report_ref, 2);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    default:
        ESP_LOGE(TAG, "unexpected access operation to Report Reference Descriptor, opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int vendor_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t *val_ptr = NULL;
    size_t val_size = 0;
    int chr_num = 0;

    if (attr_handle == vendor_chr1_val_handle)
    {
        val_ptr = vendor_chr1_val;
        val_size = sizeof(vendor_chr1_val);
        chr_num = 1;
    }
    else if (attr_handle == vendor_chr2_val_handle)
    {
        val_ptr = vendor_chr2_val;
        val_size = sizeof(vendor_chr2_val);
        chr_num = 2;
    }
    else if (attr_handle == vendor_chr3_val_handle)
    {
        val_ptr = vendor_chr3_val;
        val_size = sizeof(vendor_chr3_val);
        chr_num = 3;
    }
    else if (attr_handle == vendor_chr4_val_handle)
    {
        val_ptr = vendor_chr4_val;
        val_size = sizeof(vendor_chr4_val);
        chr_num = 4;
    }
    else if (attr_handle == vendor_chr5_val_handle)
    {
        val_ptr = vendor_chr5_val;
        val_size = sizeof(vendor_chr5_val);
        chr_num = 5;
    }
    else if (attr_handle == vendor_chr6_val_handle)
    {
        val_ptr = vendor_chr6_val;
        val_size = sizeof(vendor_chr6_val);
        chr_num = 6;
    }
    else
    {
        ESP_LOGE(TAG, "Unknown vendor characteristic handle: 0x%04X", attr_handle);
        return BLE_ATT_ERR_UNLIKELY;
    }

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Vendor Characteristic %d read; conn_handle=%d", chr_num, conn_handle);
        return os_mbuf_append(ctxt->om, val_ptr, val_size) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (ctxt->om->om_len <= val_size)
        {
            os_mbuf_copydata(ctxt->om, 0, ctxt->om->om_len, val_ptr);
            ESP_LOGI(TAG, "Vendor Characteristic %d write; conn_handle=%d len=%d",
                     chr_num, conn_handle, ctxt->om->om_len);
        }
        else
        {
            ESP_LOGW(TAG, "Vendor Characteristic %d write: data too long (%d > %d)",
                     chr_num, ctxt->om->om_len, val_size);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        return 0;
    default:
        ESP_LOGE(TAG, "unexpected access operation to Vendor Characteristic %d, opcode: %d", chr_num, ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

const ble_uuid16_t keyboard_svc_uuid = BLE_UUID16_INIT(0xFFF0);
const ble_uuid16_t keyboard_chr_uuid = BLE_UUID16_INIT(0xFFF1);

uint16_t keyboard_chr_val_handle;
uint16_t keyboard_conn_handle = BLE_HS_CONN_HANDLE_NONE;
bool keyboard_notify_enabled = false;

#define KB_QUEUE_SIZE 5
#define KB_BUFFER_SIZE 128
#define KB_TASK_DELAY_MS 1
#define KB_PRESS_MS 5

typedef struct
{
    char text[KB_BUFFER_SIZE];
    uint8_t len;
    uint8_t notify_index;
    uint8_t read_index;
} kb_item_t;

static kb_item_t kb_queue[KB_QUEUE_SIZE];
static uint8_t kb_queue_head = 0;
static uint8_t kb_queue_tail = 0;

/* kb_task_handle is now the same as keyboard_auto_send_task_handle - use that instead */

static bool kb_queue_empty()
{
    return kb_queue_head == kb_queue_tail;
}

static bool kb_queue_push(const char *txt)
{
    uint8_t next_tail = (kb_queue_tail + 1) % KB_QUEUE_SIZE;
    if (next_tail == kb_queue_head)
    {
        ESP_LOGW(TAG, "keyboard queue full!");
        return false;
    }
    kb_item_t *item = &kb_queue[kb_queue_tail];
    item->len = strnlen(txt, KB_BUFFER_SIZE);
    memcpy(item->text, txt, item->len);
    item->notify_index = 0;
    item->read_index = 0;
    kb_queue_tail = next_tail;
    return true;
}

static kb_item_t *kb_queue_front()
{
    if (kb_queue_empty())
        return NULL;
    return &kb_queue[kb_queue_head];
}

static void kb_queue_pop()
{
    if (!kb_queue_empty())
    {
        kb_queue_head = (kb_queue_head + 1) % KB_QUEUE_SIZE;
    }
}

/* Convert ASCII character to HID key code and modifiers */
static void char_to_hid_key(char c, uint8_t *key_code, uint8_t *modifiers)
{
    *modifiers = 0;
    *key_code = 0;

    if (c >= 'a' && c <= 'z')
    {
        *key_code = 0x04 + (c - 'a'); /* HID usage codes 0x04-0x1D for a-z */
    }
    else if (c >= 'A' && c <= 'Z')
    {
        *modifiers = 0x02; /* Left Shift */
        *key_code = 0x04 + (c - 'A');
    }
    else if (c >= '1' && c <= '9')
    {
        *key_code = 0x1E + (c - '1'); /* HID usage codes 0x1E-0x26 for 1-9 */
    }
    else if (c == '0')
    {
        *key_code = 0x27; /* HID usage code for 0 */
    }
    else
    {
        switch (c)
        {
        case ' ':
            *key_code = 0x2C; /* Space */
            break;
        case '\n':
        case '\r':
            *key_code = 0x28; /* Enter */
            break;
        case '\t':
            *key_code = 0x2B; /* Tab */
            break;
        case '-':
            *key_code = 0x2D;
            break;
        case '_':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x2D;
            break;
        case '=':
            *key_code = 0x2E;
            break;
        case '+':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x2E;
            break;
        case '[':
            *key_code = 0x2F;
            break;
        case '{':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x2F;
            break;
        case ']':
            *key_code = 0x30;
            break;
        case '}':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x30;
            break;
        case '\\':
            *key_code = 0x31;
            break;
        case '|':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x31;
            break;
        case ';':
            *key_code = 0x33;
            break;
        case ':':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x33;
            break;
        case '\'':
            *key_code = 0x34;
            break;
        case '"':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x34;
            break;
        case '`':
            *key_code = 0x35;
            break;
        case '~':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x35;
            break;
        case ',':
            *key_code = 0x36;
            break;
        case '<':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x36;
            break;
        case '.':
            *key_code = 0x37;
            break;
        case '>':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x37;
            break;
        case '/':
            *key_code = 0x38;
            break;
        case '?':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x38;
            break;
        case '!':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x1E;  /* 1 */
            break;
        case '@':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x1F;  /* 2 */
            break;
        case '#':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x20;  /* 3 */
            break;
        case '$':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x21;  /* 4 */
            break;
        case '%':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x22;  /* 5 */
            break;
        case '^':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x23;  /* 6 */
            break;
        case '&':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x24;  /* 7 */
            break;
        case '*':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x25;  /* 8 */
            break;
        case '(':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x26;  /* 9 */
            break;
        case ')':
            *modifiers = 0x02; /* Shift */
            *key_code = 0x27;  /* 0 */
            break;
        default:
            *key_code = 0; /* Unknown character, skip */
            break;
        }
    }
}

static void kb_task(void *arg)
{
    while (1)
    {
        kb_item_t *item = kb_queue_front();
        if (item)
        {
            if (item->read_index < item->len)
            {
                char c = item->text[item->read_index];
                item->read_index++;

                uint8_t key_code = 0;
                uint8_t modifiers = 0;
                char_to_hid_key(c, &key_code, &modifiers);

                if (key_code != 0)
                {
                    /* Check if HID notifications are enabled */
                    if ((kbd_input_notify_enabled && kbd_input_conn_handle != BLE_HS_CONN_HANDLE_NONE) ||
                        (kbd_report_notify_enabled && kbd_report_conn_handle != BLE_HS_CONN_HANDLE_NONE))
                    {
                        int rc = hid_keyboard_service_send_report(modifiers, &key_code, 1);
                        if (rc != 0)
                        {
                            ESP_LOGW(TAG, "Failed to send key press: %d", rc);
                        }

                        /* Wait before releasing */
                        vTaskDelay(pdMS_TO_TICKS(15));

                        /* Send key release (empty report) */
                        uint8_t empty_keys[6] = {0};
                        rc = hid_keyboard_service_send_report(0, empty_keys, 0);
                        if (rc != 0)
                        {
                            ESP_LOGW(TAG, "Failed to send key release: %d", rc);
                        }
                    }
                }
            }

            if (item->read_index >= item->len)
            {
                kb_queue_pop();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void keyboard_set_text(const char *txt)
{
    if (!kb_queue_push(txt))
    {
        ESP_LOGW(TAG, "Failed to enqueue keyboard text");
        return;
    }

    /* Task should already be created by subscribe callback when notifications are enabled */
    /* Just queue the text - the task will process it */
    ESP_LOGI(TAG, "Text queued: \"%s\"", txt);
}

int keyboard_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
    {
        kb_item_t *item = kb_queue_front();
        if (!item || item->read_index == 0)
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        ESP_LOGI(TAG, "keyboard read; handle=%d, read_index=%d", attr_handle, item->read_index);
        rc = os_mbuf_append(ctxt->om, item->text, item->read_index);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    default:
        ESP_LOGE(TAG, "unexpected op %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* Public functions */
const struct ble_gatt_svc_def *hid_keyboard_service_get_svc_def(void)
{
    return hid_keyboard_svc_def;
}

int hid_keyboard_service_init(void)
{
    return 0;
}

void hid_keyboard_service_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
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
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "registering descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;
    default:
        break;
    }
}

void hid_keyboard_service_subscribe_cb(struct ble_gap_event *event)
{
    ESP_LOGI(TAG, "Subscribe check: attr_handle=%d, cur_notify=%d", event->subscribe.attr_handle, event->subscribe.cur_notify);

    if (event->subscribe.attr_handle == boot_kbd_input_chr_val_handle)
    {
        ESP_LOGI(TAG, "Subscription to Boot Keyboard Input Report (0x2A22)");
        kbd_input_conn_handle = event->subscribe.conn_handle;
        kbd_input_notify_enabled = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "Boot Keyboard Input Report notifications %s",
                 kbd_input_notify_enabled ? "enabled" : "disabled");
    }
    else if (event->subscribe.attr_handle == hid_report_chr_val_handle)
    {
        ESP_LOGI(TAG, "Subscription to Report characteristic (0x2A4D)");
        kbd_report_conn_handle = event->subscribe.conn_handle;
        kbd_report_notify_enabled = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "Report characteristic notifications %s",
                 kbd_report_notify_enabled ? "enabled" : "disabled");
    }

    /* Create keyboard task when HID notifications are enabled */
    if ((kbd_input_notify_enabled || kbd_report_notify_enabled) && keyboard_auto_send_task_handle == NULL)
    {
        ESP_LOGI(TAG, "Creating keyboard task for text sending...");
        BaseType_t result = xTaskCreate(kb_task, "keyboard_task", 4096, NULL, tskIDLE_PRIORITY + 1, &keyboard_auto_send_task_handle);
        if (result != pdPASS || keyboard_auto_send_task_handle == NULL)
        {
            ESP_LOGE(TAG, "Failed to create keyboard task (result=%d)", result);
        }
        else
        {
            ESP_LOGI(TAG, "Keyboard task created successfully");
        }
    }
    /* Delete keyboard task when HID notifications are disabled */
    else if (!kbd_input_notify_enabled && !kbd_report_notify_enabled && keyboard_auto_send_task_handle != NULL)
    {
        vTaskDelete(keyboard_auto_send_task_handle);
        keyboard_auto_send_task_handle = NULL;
        ESP_LOGI(TAG, "Keyboard task deleted (both notifications disabled)");
    }
}

int hid_keyboard_service_send_report(uint8_t modifiers, uint8_t *key_codes, uint8_t key_count)
{
    struct os_mbuf *om;
    int rc = BLE_HS_ENOTCONN;
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
    uint16_t chr_val_handle = 0;

    if (key_count > 6)
    {
        key_count = 6;
    }

    boot_kbd_input_report[0] = modifiers;
    boot_kbd_input_report[1] = 0;
    memset(&boot_kbd_input_report[2], 0, 6);
    if (key_codes != NULL && key_count > 0)
    {
        memcpy(&boot_kbd_input_report[2], key_codes, key_count);
    }

    if (kbd_report_notify_enabled && kbd_report_conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        conn_handle = kbd_report_conn_handle;
        chr_val_handle = hid_report_chr_val_handle;
    }
    else if (kbd_input_notify_enabled && kbd_input_conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        conn_handle = kbd_input_conn_handle;
        chr_val_handle = boot_kbd_input_chr_val_handle;
    }
    else
    {
        return BLE_HS_ENOTCONN;
    }

    om = ble_hs_mbuf_from_flat(boot_kbd_input_report, sizeof(boot_kbd_input_report));
    if (om == NULL)
    {
        return BLE_HS_ENOMEM;
    }

    rc = ble_gatts_notify_custom(conn_handle, chr_val_handle, om);
    if (rc != 0)
    {
        os_mbuf_free_chain(om);
    }

    return rc;
}
