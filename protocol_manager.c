#include <string.h>
#include <stdlib.h>
#include "protocol_manager.h"
#include "esp_log.h"

#define WEB_SERVER "sieci.kis.agh.edu.pl"
#define WEB_PORT "80"
#define WEB_PATH "/"

static const char *TAG = "protocol_manager";

int tcp_connect(const char *host, int port)
{
    struct sockaddr_in server_addr;
    int sock;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);

    connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

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