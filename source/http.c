#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <switch.h>

#include <sys/socket.h>

static bool send_all(
    int sock,
    const void *data,
    size_t length)
{
    const char *ptr = data;

    while (length > 0)
    {
        ssize_t sent = send(sock, ptr, length, 0);

        if (sent <= 0)
            return false;

        ptr += sent;
        length -= (size_t)sent;
    }

    return true;
}

static bool read_headers(
    int sock,
    char *buffer,
    size_t capacity)
{
    size_t length = 0;

    while (length + 1 < capacity)
    {
        char c;

        ssize_t received =
            recv(sock, &c, 1, 0);

        if (received <= 0)
            return false;

        buffer[length++] = c;

        if (length >= 4 &&
            buffer[length - 4] == '\r' &&
            buffer[length - 3] == '\n' &&
            buffer[length - 2] == '\r' &&
            buffer[length - 1] == '\n')
        {
            buffer[length] = '\0';
            return true;
        }
    }

    return false;
}

static const char *find_header(
    const char *headers,
    const char *name)
{
    size_t name_length = strlen(name);

    const char *line = headers;

    while (*line)
    {
        const char *line_end =
            strstr(line, "\r\n");

        if (!line_end)
            break;

        if ((size_t)(line_end - line) >= name_length &&
            strncasecmp(
                line,
                name,
                name_length
            ) == 0)
        {
            const char *p =
                line + name_length;

            while (p < line_end &&
                   (*p == ' ' || *p == '\t'))
            {
                p++;
            }

            if (p < line_end && *p == ':')
            {
                p++;

                while (p < line_end &&
                       (*p == ' ' || *p == '\t'))
                {
                    p++;
                }

                return p;
            }
        }

        line = line_end + 2;
    }

    return NULL;
}

static uint64_t parse_content_length(
    const char *headers)
{
    const char *p =
        find_header(
            headers,
            "Content-Length"
        );

    if (!p)
        return 0;

    return strtoull(
        p,
        NULL,
        10
    );
}

bool http_read_request(
    int sock,
    HttpRequest *request)
{
    char headers[HTTP_HEADER_MAX];

    if (!read_headers(
            sock,
            headers,
            sizeof(headers)))
    {
        return false;
    }

    const char *line_end =
        strstr(headers, "\r\n");

    if (!line_end)
        return false;

    char line[2048];

    size_t line_length =
        (size_t)(line_end - headers);

    if (line_length >= sizeof(line))
        return false;

    memcpy(
        line,
        headers,
        line_length
    );

    line[line_length] = '\0';

    if (sscanf(
            line,
            "%15s %1023s",
            request->method,
            request->path) != 2)
    {
        return false;
    }

    request->content_length =
        parse_content_length(headers);

    printf(
        "\nHTTP: %s %s\n",
        request->method,
        request->path
    );

    printf(
        "Content-Length: %llu\n",
        (unsigned long long)
            request->content_length
    );

    consoleUpdate(NULL);

    return true;
}

bool http_read_body(
    int sock,
    uint64_t length,
    char **body)
{
    if (length > HTTP_BODY_MAX)
        return false;

    char *buffer =
        malloc((size_t)length + 1);

    if (!buffer)
        return false;

    size_t received = 0;

    while (received < length)
    {
        ssize_t result =
            recv(
                sock,
                buffer + received,
                (size_t)(length - received),
                0
            );

        if (result <= 0)
        {
            free(buffer);
            return false;
        }

        received += (size_t)result;
    }

    buffer[length] = '\0';

    *body = buffer;

    return true;
}

bool http_send(
    int sock,
    int status,
    const char *status_text,
    const char *content_type,
    const char *body)
{
    char header[1024];

    size_t body_length =
        strlen(body);

    int length = snprintf(
        header,
        sizeof(header),

        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",

        status,
        status_text,
        content_type,
        body_length
    );

    if (length <= 0)
        return false;

    if (!send_all(
            sock,
            header,
            (size_t)length))
    {
        return false;
    }

    return send_all(
        sock,
        body,
        body_length
    );
}

bool http_send_json(
    int sock,
    const char *json)
{
    return http_send(
        sock,
        200,
        "OK",
        "application/json",
        json
    );
}