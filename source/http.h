#ifndef LOCALSNX_HTTP_H
#define LOCALSNX_HTTP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define HTTP_HEADER_MAX 16384
#define HTTP_BODY_MAX   65536

typedef struct
{
    char method[16];
    char path[1024];
    uint64_t content_length;
} HttpRequest;

bool http_read_request(int sock, HttpRequest *request);

bool http_read_body(
    int sock,
    uint64_t length,
    char **body
);

bool http_send(
    int sock,
    int status,
    const char *status_text,
    const char *content_type,
    const char *body
);

bool http_send_json(
    int sock,
    const char *json
);

#endif