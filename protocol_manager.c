#include <string.h>
#include <stdlib.h>
#include "protocol_manager.h"
#include "esp_log.h"


static const char *TAG = "protocol_manager";

int tcp_connector(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int sock;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for host %s", host);
        return -1;
    }

    sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "Socket connect failed");
        close(sock);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return sock;
}


char *http_get(int sock, const char *hostname, const char *path)
{
    char request[512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, hostname);

    send(sock, request, strlen(request), 0);

    char *buffer = malloc(4096);
    if (buffer == NULL)
    {
        return NULL;
    }

    int bytes_received = recv(sock, buffer, 4095, 0);
    if (bytes_received < 0)
    {
        free(buffer);
        return NULL;
    }

    buffer[bytes_received] = '\0';
    return buffer;
}

void tcp_disconnect(int sock)
{
    close(sock);
}