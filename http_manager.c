#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "http_manager.h"

#define MAX_HTTP_OUTPUT_BUFFER 8192

static const char *TAG = "http_manager";

esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;

    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;

    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;

    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (output_len == 0 && evt->user_data)
        {
            memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
        }
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            int copy_len = 0;
            if (evt->user_data)
            {
                copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                if (copy_len)
                {
                    memcpy(evt->user_data + output_len, evt->data, copy_len);
                }
            }
            else
            {
                int content_len = esp_http_client_get_content_length(evt->client);
                if (output_buffer == NULL)
                {
                    output_buffer = (char *)calloc(content_len + 1, sizeof(char));
                    output_len = 0;
                    if (output_buffer == NULL)
                    {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                copy_len = MIN(evt->data_len, (content_len - output_len));
                if (copy_len)
                {
                    memcpy(output_buffer + output_len, evt->data, copy_len);
                }
            }
            output_len += copy_len;
        }

        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
#if CONFIG_EXAMPLE_ENABLE_RESPONSE_BUFFER_DUMP
            ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
#endif
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;

    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "From", "user@example.com");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    return ESP_OK;
}

void http_get_and_print_html(void)
{
    char *local_response_buffer = malloc(MAX_HTTP_OUTPUT_BUFFER + 1);
    if (!local_response_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
        return;
    }
    memset(local_response_buffer, 0, MAX_HTTP_OUTPUT_BUFFER + 1);

    esp_http_client_config_t config = {
        .url = "http://sieci.kis.agh.edu.pl/",
        // .host = CONFIG_EXAMPLE_HTTP_ENDPOINT,
        // .path = "/get",
        // .query = "esp",
        .event_handler = http_event_handler,
        .user_data = local_response_buffer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));

        size_t response_len = strlen(local_response_buffer);
        size_t chunk_size = 512;
        for (size_t offset = 0; offset < response_len; offset += chunk_size)
        {
            size_t this_chunk = (response_len - offset > chunk_size) ? chunk_size : (response_len - offset);
            ESP_LOGI(TAG, "HTTP Response (bytes %d-%d):\n%.*s",
                     (int)offset, (int)(offset + this_chunk - 1), (int)this_chunk, local_response_buffer + offset);
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(local_response_buffer);
}
