#include "localsend.h"
#include "http.h"
#include "transfer.h"
#include "sender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <switch.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static int server_socket = -1;
static uint64_t last_announcement = 0;

static Thread server_thread;
static bool server_thread_created = false;
static volatile bool server_thread_running = false;

static void localsend_server_thread(void *arg)
{
    (void)arg;

    server_thread_running = true;

    while (server_thread_running)
    {
        if (server_socket < 0)
            break;

        struct sockaddr_in client_address;

        socklen_t client_length =
            sizeof(client_address);

        int client =
            accept(
                server_socket,
                (struct sockaddr *)&client_address,
                &client_length
            );

        if (client < 0)
        {
            if (
                errno == EAGAIN ||
                errno == EWOULDBLOCK
            )
            {
                svcSleepThread(
                    10 * 1000 * 1000
                );

                continue;
            }

            if (!server_thread_running)
                break;

            svcSleepThread(
                10 * 1000 * 1000
            );

            continue;
        }

        /*
         * Protect the HTTP request from hanging
         * forever if the client disappears.
         */

        struct timeval client_timeout;

        client_timeout.tv_sec = 30;
        client_timeout.tv_usec = 0;

        setsockopt(
            client,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &client_timeout,
            sizeof(client_timeout)
        );

        localsend_handle_client(
            client
        );

        close(client);
    }

    server_thread_running = false;

    threadExit();
}

static const char *device_json(void)
{
    return
        "{"
        "\"alias\":\"LocalS-NX\","
        "\"version\":\"2.2\","
        "\"deviceModel\":\"Nintendo Switch\","
        "\"deviceType\":\"mobile\","
        "\"fingerprint\":\"LocalS-NX-Switch\","
        "\"port\":53317,"
        "\"protocol\":\"http\","
        "\"download\":false"
        "}";
}

void localsend_init(void)
{
    transfer_init();
    sender_init();
}

bool localsend_start_server(void)
{
    if (server_socket >= 0)
        return true;

    server_socket =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (server_socket < 0)
    {
        return false;
    }

    int reuse = 1;

    setsockopt(
        server_socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse)
    );

    /*
     * The server thread uses non-blocking
     * accept() so it can shut down cleanly.
     */

    int flags =
        fcntl(
            server_socket,
            F_GETFL,
            0
        );

    if (flags < 0)
    {
        close(server_socket);
        server_socket = -1;
        return false;
    }

    if (
        fcntl(
            server_socket,
            F_SETFL,
            flags | O_NONBLOCK
        ) < 0
    )
    {
        close(server_socket);
        server_socket = -1;
        return false;
    }

    struct sockaddr_in address;

    memset(
        &address,
        0,
        sizeof(address)
    );

    address.sin_family =
        AF_INET;

    address.sin_port =
        htons(LOCALSEND_PORT);

    address.sin_addr.s_addr =
        htonl(INADDR_ANY);

    if (
        bind(
            server_socket,
            (struct sockaddr *)&address,
            sizeof(address)
        ) < 0
    )
    {
        close(server_socket);
        server_socket = -1;
        return false;
    }

    if (
        listen(
            server_socket,
            8
        ) < 0
    )
    {
        close(server_socket);
        server_socket = -1;
        return false;
    }

    /*
     * Start the LocalSend TCP server thread.
     */

    Result rc =
        threadCreate(
            &server_thread,
            localsend_server_thread,
            NULL,
            NULL,
            128 * 1024,
            0x2A,
            -2
        );

    if (R_FAILED(rc))
    {
        close(server_socket);
        server_socket = -1;
        return false;
    }

    server_thread_created = true;

    rc =
        threadStart(
            &server_thread
        );

    if (R_FAILED(rc))
    {
        server_thread_running = false;

        threadClose(
            &server_thread
        );

        server_thread_created = false;

        close(server_socket);
        server_socket = -1;

        return false;
    }

    last_announcement = 0;

    return true;
}

void localsend_poll(void)
{
    if (server_socket < 0)
        return;

    /*
     * Re-announce ourselves every 5 seconds.
     *
     * TCP connections are handled entirely by
     * localsend_server_thread(), so this function
     * never blocks the Plutonium render loop.
     */

    uint64_t now =
        armGetSystemTick();

    uint64_t interval =
        armGetSystemTickFreq() * 5;

    if (
        last_announcement == 0 ||
        now - last_announcement >= interval
    )
    {
        localsend_announce();

        last_announcement =
            now;
    }
}

void localsend_discovery_poll(void)
{
    /*
     * TCP discovery/registration is now handled
     * by the LocalSend server thread.
     *
     * Keep this function for API compatibility.
     */
}

void localsend_stop_server(void)
{
    server_thread_running = false;

    if (server_socket >= 0)
    {
        shutdown(
            server_socket,
            SHUT_RDWR
        );

        close(
            server_socket
        );

        server_socket = -1;
    }

    if (server_thread_created)
    {
        threadWaitForExit(
            &server_thread
        );

        threadClose(
            &server_thread
        );

        server_thread_created = false;
    }
}

void localsend_announce(void)
{
    int socket_fd =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );

    if (socket_fd < 0)
        return;

    int ttl = 1;

    setsockopt(
        socket_fd,
        IPPROTO_IP,
        IP_MULTICAST_TTL,
        &ttl,
        sizeof(ttl)
    );

    struct sockaddr_in address;

    memset(
        &address,
        0,
        sizeof(address)
    );

    address.sin_family =
        AF_INET;

    address.sin_port =
        htons(LOCALSEND_PORT);

    inet_aton(
        LOCALSEND_MULTICAST,
        &address.sin_addr
    );

    const char *json =
        device_json();

    sendto(
        socket_fd,
        json,
        strlen(json),
        0,
        (struct sockaddr *)&address,
        sizeof(address)
    );

    close(socket_fd);
}

void localsend_handle_client(
    int client
)
{
    HttpRequest request;

    if (
        !http_read_request(
            client,
            &request
        )
    )
    {
        return;
    }

    /*
     * IMPORTANT:
     *
     * Upload is handled before reading
     * the HTTP body because the body is
     * potentially gigabytes in size.
     */

    if (
        strcmp(
            request.method,
            "POST"
        ) == 0 &&
        strncmp(
            request.path,
            "/api/localsend/v2/upload",
            strlen(
                "/api/localsend/v2/upload"
            )
        ) == 0
    )
    {
        transfer_upload(
            client,
            request.path,
            request.content_length
        );

        return;
    }

    char *body = NULL;

    if (
        request.content_length > 0
    )
    {
        if (
            !http_read_body(
                client,
                request.content_length,
                &body
            )
        )
        {
            http_send(
                client,
                400,
                "Bad Request",
                "text/plain",
                "Invalid request body"
            );

            return;
        }
    }
    else
    {
        body =
            strdup("");
    }

    /*
     * LocalSend device registration.
     */

    if (
        strcmp(
            request.method,
            "POST"
        ) == 0 &&
        strcmp(
            request.path,
            "/api/localsend/v2/register"
        ) == 0
    )
    {
        struct sockaddr_in peer;

        memset(
            &peer,
            0,
            sizeof(peer)
        );

        socklen_t peer_length =
            sizeof(peer);

        char peer_ip[64];

        memset(
            peer_ip,
            0,
            sizeof(peer_ip)
        );

        if (
            getpeername(
                client,
                (struct sockaddr *)&peer,
                &peer_length
            ) == 0
        )
        {
            snprintf(
                peer_ip,
                sizeof(peer_ip),
                "%s",
                inet_ntoa(
                    peer.sin_addr
                )
            );
        }
        else
        {
            snprintf(
                peer_ip,
                sizeof(peer_ip),
                "unknown"
            );
        }

        sender_handle_register(
            client,
            body,
            peer_ip
        );
    }
    else if (
        strcmp(
            request.method,
            "GET"
        ) == 0 &&
        strcmp(
            request.path,
            "/api/localsend/v2/register"
        ) == 0
    )
    {
        http_send_json(
            client,
            device_json()
        );
    }
    else if (
        strcmp(
            request.method,
            "POST"
        ) == 0 &&
        strcmp(
            request.path,
            "/api/localsend/v2/prepare-upload"
        ) == 0
    )
    {
        transfer_prepare(
            client,
            body
        );
    }
    else if (
        strcmp(
            request.method,
            "POST"
        ) == 0 &&
        strcmp(
            request.path,
            "/api/localsend/v2/cancel"
        ) == 0
    )
    {
        transfer_cancel(
            client,
            body
        );
    }
    else
    {
        http_send(
            client,
            404,
            "Not Found",
            "text/plain",
            "Not Found"
        );
    }

    free(body);
}