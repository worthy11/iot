#ifndef PROTOCOL_MANAGER_H
#define PROTOCOL_MANAGER_H

#include "esp_system.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

int tcp_connector(const char *host, const char *port);
char *http_get(int sock, const char *hostname, const char *path);
void tcp_disconnect(int sock);

#endif // PROTOCOL_MANAGER_H