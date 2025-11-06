#ifndef PROTOCOL_MANAGER_H
#define PROTOCOL_MANAGER_H

int tcp_connector(const char *host, const char *port);
char *http_get(int sock, const char *hostname, const char *path);
void tcp_disconnect(int sock);

#endif // PROTOCOL_MANAGER_H