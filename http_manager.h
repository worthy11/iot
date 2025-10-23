#ifndef HTTP_MANAGER_H
#define HTTP_MANAGER_H

#include "esp_err.h"
#include "esp_http_client.h"

esp_err_t http_event_handler(esp_http_client_event_t *evt);
void http_get_and_print_html(void);

#endif