#ifndef HTTP_MANAGER_H
#define HTTP_MANAGER_H

#include "esp_system.h"
#include "esp_event.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

void http_get_task(void *pvParameters);

#endif // HTTP_MANAGER_H