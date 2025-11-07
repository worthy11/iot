#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "protocol_manager.h"

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
    if (err != 0 || res == NULL)
    {
        ESP_LOGE(TAG, "DNS lookup failed for host %s", host);
        return -1;
    }

    sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0)
    {
        ESP_LOGE(TAG, "Socket connect failed");
        close(sock);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    ESP_LOGI(TAG, "Socket connect successful");
    return sock;
}

char *http_get(int sock, const char *hostname, const char *path)
{
    int request_size = snprintf(NULL, 0,
                                "GET %s HTTP/1.1\r\n"
                                "Host: %s\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                                path, hostname) +
                       1;

    char *request = malloc(request_size);
    if (request == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate request buffer");
        return NULL;
    }

    snprintf(request, request_size,
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, hostname);

    if (send(sock, request, strlen(request), 0) < 0)
    {
        ESP_LOGE(TAG, "Failed to send HTTP request");
        free(request);
        return NULL;
    }

    free(request);

    size_t buffer_size = 8192;
    size_t total_received = 0;
    char *buffer = malloc(buffer_size);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return NULL;
    }

    while (1)
    {
        int bytes_received = recv(sock, buffer + total_received, buffer_size - total_received - 1, 0);

        if (bytes_received < 0)
        {
            ESP_LOGE(TAG, "Receive failed");
            free(buffer);
            return NULL;
        }

        if (bytes_received == 0)
        {
            break;
        }

        total_received += bytes_received;

        if (total_received >= buffer_size - 1)
        {
            buffer_size *= 2;
            char *new_buffer = realloc(buffer, buffer_size);
            if (new_buffer == NULL)
            {
                ESP_LOGE(TAG, "Failed to expand buffer");
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }
    }

    buffer[total_received] = '\0';
    ESP_LOGI(TAG, "Received %d bytes", total_received);
    return buffer;
}

void tcp_disconnect(int sock)
{
    if (sock >= 0)
    {
        close(sock);
        ESP_LOGI(TAG, "Socket close successful");
    }
}