#include "keyboard_service.h"
#include "esp_log.h"
#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "keyboard_service";

const ble_uuid16_t keyboard_svc_uuid = BLE_UUID16_INIT(0xFFF0);
const ble_uuid16_t keyboard_chr_uuid = BLE_UUID16_INIT(0xFFF1);

uint16_t keyboard_chr_val_handle;
uint16_t keyboard_conn_handle = BLE_HS_CONN_HANDLE_NONE;
bool keyboard_notify_enabled = false;

#define KB_QUEUE_SIZE 5
#define KB_BUFFER_SIZE 128
#define KB_TASK_DELAY_MS 1000
#define KB_PRESS_MS 200

typedef struct {
    char text[KB_BUFFER_SIZE];
    uint8_t len;
    uint8_t notify_index;  
    uint8_t read_index;    
} kb_item_t;

static kb_item_t kb_queue[KB_QUEUE_SIZE];
static uint8_t kb_queue_head = 0;
static uint8_t kb_queue_tail = 0;

static TaskHandle_t kb_task_handle = NULL;

static bool kb_queue_empty() {
    return kb_queue_head == kb_queue_tail;
}

static bool kb_queue_push(const char *txt) {
    uint8_t next_tail = (kb_queue_tail + 1) % KB_QUEUE_SIZE;
    if (next_tail == kb_queue_head) {
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

static kb_item_t* kb_queue_front() {
    if (kb_queue_empty()) return NULL;
    return &kb_queue[kb_queue_head];
}

static void kb_queue_pop() {
    if (!kb_queue_empty()) {
        kb_queue_head = (kb_queue_head + 1) % KB_QUEUE_SIZE;
    }
}

static void kb_task(void *arg) {
    while (1) {
        kb_item_t *item = kb_queue_front();
        if (item) {
            if (item->read_index < item->len) {
                uint8_t c = item->text[item->read_index];
                item->read_index++;

                if (keyboard_notify_enabled && keyboard_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                    ESP_LOGI(TAG, "kb notify: conn=%d, char=0x%02x", keyboard_conn_handle, c);
                    struct os_mbuf *om = ble_hs_mbuf_from_flat(&c, 1);
                    if (om == NULL) {
                        ESP_LOGE(TAG, "failed to alloc mbuf for key press");
                    } else {
                        int rc = ble_gatts_notify_custom(keyboard_conn_handle, keyboard_chr_val_handle, om);
                        if (rc != 0) {
                            ESP_LOGE(TAG, "ble_gatts_notify_custom press rc=%d", rc);
                        }
                    }

                    vTaskDelay(pdMS_TO_TICKS(KB_PRESS_MS));

                    uint8_t release = 0;
                    ESP_LOGI(TAG, "kb notify: conn=%d, release=0", keyboard_conn_handle);
                    struct os_mbuf *om_release = ble_hs_mbuf_from_flat(&release, 1);
                    if (om_release == NULL) {
                        ESP_LOGE(TAG, "failed to alloc mbuf for key release");
                    } else {
                        int rc2 = ble_gatts_notify_custom(keyboard_conn_handle, keyboard_chr_val_handle, om_release);
                        if (rc2 != 0) {
                            ESP_LOGE(TAG, "ble_gatts_notify_custom release rc=%d", rc2);
                        }
                    }
                }
            }

            if (item->read_index >= item->len) {
                kb_queue_pop();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(KB_TASK_DELAY_MS));
    }
}


void keyboard_set_text(const char *txt) {
    if (!kb_queue_push(txt)) {
        ESP_LOGW(TAG, "Failed to enqueue keyboard text");
        return;
    }

    if (kb_task_handle == NULL) {
        BaseType_t res = xTaskCreate(
            kb_task,
            "keyboard_task",
            4096,             
            NULL,
            tskIDLE_PRIORITY + 1,
            &kb_task_handle
        );
        if (res != pdPASS) {
            ESP_LOGE(TAG, "Failed to create keyboard task");
        }
    }
}

int keyboard_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        kb_item_t *item = kb_queue_front();
        if (!item || item->read_index == 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
        ESP_LOGI(TAG, "keyboard read; handle=%d, read_index=%d", attr_handle, item->read_index);
        rc = os_mbuf_append(ctxt->om, item->text, item->read_index);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    default:
        ESP_LOGE(TAG, "unexpected op %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}



