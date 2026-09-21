#include "sender.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#include <switch.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <fcntl.h>
#include <errno.h>

#include "http.h"
#include "localsend.h"
#include "sha256.h"

#define LOCALSEND_MULTICAST "224.0.0.167"
#define LOCALSEND_PORT 53317

#define DISCOVERY_TIME_NS 3000000000ULL

#define SENDER_BUFFER_SIZE 65536
#define SENDER_JSON_MAX 262144

#define SENDER_MAX_FILES 256

typedef struct
{
    char id[64];
    char token[128];

} SenderUploadToken;

static SenderDevice devices[
    SENDER_MAX_DEVICES
];

static size_t device_count = 0;
static SenderTransferState transfer_state;

static Thread transfer_thread;

static bool transfer_thread_created = false;
static bool transfer_thread_running = false;
static volatile bool transfer_cancel_requested = false;

static int discovery_socket = -1;

#define SENDER_TRANSFER_MAX_PATHS 256

typedef struct
{
    const SenderDevice *device;

    char paths[SENDER_TRANSFER_MAX_PATHS][1024];

    const char *path_ptrs[SENDER_TRANSFER_MAX_PATHS];

    size_t path_count;

} SenderTransferJob;

static SenderTransferJob transfer_job;

static void transfer_set_stage(
    const char *stage)
{
    if (!stage)
        return;

    snprintf(
        transfer_state.stage,
        sizeof(transfer_state.stage),
        "%s",
        stage
    );
}




/*
 * ---------------------------------------------------------
 * Transfer worker
 * ---------------------------------------------------------
 */

static void transfer_thread_entry(void *arg)
{
    SenderTransferJob *job =
        (SenderTransferJob *)arg;

    transfer_set_stage(
        "Worker thread started"
    );

    transfer_set_stage(
        "Starting sender..."
    );

    sender_send_files(
        job->device,
        job->path_ptrs,
        job->path_count
    );

    transfer_thread_running =
        false;

    threadExit();
}


/*
 * Start a transfer on a worker thread.
 */

bool sender_start_transfer(
    const SenderDevice *device,
    const char *paths[],
    size_t path_count)
{

    transfer_cancel_requested = false;

    if (!device ||
        !paths ||
        path_count == 0 ||
        path_count > SENDER_TRANSFER_MAX_PATHS)
    {
        return false;
    }

    if (transfer_thread_running)
    {
        return false;
    }

    transfer_job.device =
        device;

    transfer_job.path_count =
        path_count;

    for (size_t i = 0;
         i < path_count;
         i++)
    {
        snprintf(
            transfer_job.paths[i],
            sizeof(transfer_job.paths[i]),
            "%s",
            paths[i]
        );

        transfer_job.path_ptrs[i] =
            transfer_job.paths[i];
    }

    Result rc =
        threadCreate(
            &transfer_thread,
            transfer_thread_entry,
            &transfer_job,
            NULL,
            128 * 1024,
            0x2B,
            -2
        );

    if (R_FAILED(rc))
    {
        return false;
    }

    transfer_thread_created =
        true;

    transfer_thread_running =
        true;

    rc =
        threadStart(
            &transfer_thread
        );

    if (R_FAILED(rc))
    {
        transfer_thread_running =
            false;

        threadClose(
            &transfer_thread
        );

        transfer_thread_created =
            false;

        return false;
    }

    return true;
}


bool sender_transfer_running(void)
{
    return transfer_thread_running;
}

void sender_cancel_transfer(void)
{
    if (!transfer_thread_running)
        return;

    transfer_cancel_requested = true;
}

bool sender_transfer_cancel_requested(void)
{
    return transfer_cancel_requested;
}


void sender_wait_for_transfer(void)
{
    if (!transfer_thread_created)
    {
        return;
    }

    threadWaitForExit(
        &transfer_thread
    );

    threadClose(
        &transfer_thread
    );

    transfer_thread_created =
        false;

    transfer_thread_running =
        false;
}

/*
 * ---------------------------------------------------------
 * Device discovery helpers
 * ---------------------------------------------------------
 */

 void sender_clear_devices(void)
{
    memset(
        devices,
        0,
        sizeof(devices)
    );

    device_count = 0;
}


void sender_reset_transfer_state(void)
{
    memset(
        &transfer_state,
        0,
        sizeof(transfer_state)
    );

    transfer_state.status =
        SENDER_TRANSFER_IDLE;
}


void sender_init(void)
{
    sender_clear_devices();
    sender_reset_transfer_state();
}


const SenderTransferState *sender_get_transfer_state(void)
{
    return &transfer_state;
}

static bool sender_start_discovery_socket(void)
{
    if (discovery_socket >= 0)
        return true;

    discovery_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (discovery_socket < 0)
        return false;

    int reuse = 1;
    setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));

    struct sockaddr_in local_address;
    memset(&local_address, 0, sizeof(local_address));

    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons(LOCALSEND_PORT);

    if (bind(discovery_socket,
             (struct sockaddr *)&local_address,
             sizeof(local_address)) < 0)
    {
        close(discovery_socket);
        discovery_socket = -1;
        return false;
    }

    struct ip_mreq membership;
    memset(&membership, 0, sizeof(membership));

    membership.imr_multiaddr.s_addr =
        inet_addr(LOCALSEND_MULTICAST);

    membership.imr_interface.s_addr =
        htonl(INADDR_ANY);

    if (setsockopt(discovery_socket,
                   IPPROTO_IP,
                   IP_ADD_MEMBERSHIP,
                   &membership,
                   sizeof(membership)) < 0)
    {
        close(discovery_socket);
        discovery_socket = -1;
        return false;
    }

    int flags = fcntl(discovery_socket, F_GETFL, 0);
    if (flags >= 0)
        fcntl(discovery_socket, F_SETFL, flags | O_NONBLOCK);

    return true;
}

static bool parse_device(
    SenderDevice *device,
    const char *json,
    const char *ip
);

static void add_device(
    const SenderDevice *device
);

static void sender_poll_discovery(void)
{
    if (discovery_socket < 0)
        return;

    char buffer[SENDER_JSON_MAX];

    struct sockaddr_in sender_address;
    socklen_t sender_length = sizeof(sender_address);

    ssize_t received = recvfrom(
        discovery_socket,
        buffer,
        sizeof(buffer) - 1,
        0,
        (struct sockaddr *)&sender_address,
        &sender_length
    );

    if (received <= 0)
        return;

    buffer[received] = '\0';

    char ip[64];

    snprintf(
        ip,
        sizeof(ip),
        "%s",
        inet_ntoa(sender_address.sin_addr)
    );

    SenderDevice device;

    if (parse_device(&device, buffer, ip))
        add_device(&device);
}

void sender_discovery_start(void)
{
    sender_clear_devices();

    sender_start_discovery_socket();

    sender_announce();
}

void sender_discovery_poll(void)
{
    if (discovery_socket < 0)
        return;

    for (int i = 0; i < 16; i++)
        sender_poll_discovery();
}

void sender_discovery_stop(void)
{
    if (discovery_socket < 0)
        return;

    close(discovery_socket);
    discovery_socket = -1;
}

void sender_announce(void)
{
    localsend_announce();
}

static bool json_string(
    const char *json,
    const char *key,
    char *output,
    size_t output_size)
{
    if (!json ||
        !key ||
        !output ||
        output_size == 0)
    {
        return false;
    }

    char search[128];

    int search_length =
        snprintf(
            search,
            sizeof(search),
            "\"%s\"",
            key
        );

    if (search_length <= 0 ||
        (size_t)search_length >= sizeof(search))
    {
        return false;
    }

    const char *key_position =
        strstr(
            json,
            search
        );

    if (!key_position)
        return false;

    const char *colon =
        strchr(
            key_position + search_length,
            ':'
        );

    if (!colon)
        return false;

    const char *quote =
        strchr(
            colon + 1,
            '"'
        );

    if (!quote)
        return false;

    quote++;

    const char *end =
        strchr(
            quote,
            '"'
        );

    if (!end)
        return false;

    size_t length =
        (size_t)(end - quote);

    if (length >= output_size)
        return false;

    memcpy(
        output,
        quote,
        length
    );

    output[length] = '\0';

    return true;
}


static bool parse_device(
    SenderDevice *device,
    const char *json,
    const char *ip)
{
    if (!device ||
        !json ||
        !ip)
    {
        return false;
    }

    memset(
        device,
        0,
        sizeof(SenderDevice)
    );

    if (!json_string(
            json,
            "alias",
            device->alias,
            sizeof(device->alias)))
    {
        return false;
    }

    if (!json_string(
            json,
            "version",
            device->version,
            sizeof(device->version)))
    {
        return false;
    }

    if (!json_string(
            json,
            "deviceModel",
            device->device_model,
            sizeof(device->device_model)))
    {
        return false;
    }

    if (!json_string(
            json,
            "deviceType",
            device->device_type,
            sizeof(device->device_type)))
    {
        return false;
    }

    if (!json_string(
            json,
            "fingerprint",
            device->fingerprint,
            sizeof(device->fingerprint)))
    {
        return false;
    }

    if (!json_string(
            json,
            "protocol",
            device->protocol,
            sizeof(device->protocol)))
    {
        return false;
    }

    snprintf(
        device->ip,
        sizeof(device->ip),
        "%s",
        ip
    );

    /*
     * Port is numeric rather than a JSON string.
     */
    const char *port =
        strstr(
            json,
            "\"port\""
        );

    if (!port)
        return false;

    port =
        strchr(
            port,
            ':'
        );

    if (!port)
        return false;

    unsigned long parsed_port =
        strtoul(
            port + 1,
            NULL,
            10
        );

    if (parsed_port == 0 ||
        parsed_port > 65535)
    {
        return false;
    }

    device->port =
        (uint16_t)parsed_port;

    /*
     * "download" is optional in practice, so default
     * to false if it isn't present.
     */
    device->download = false;

    const char *download =
        strstr(
            json,
            "\"download\""
        );

    if (download)
    {
        download =
            strchr(
                download,
                ':'
            );

        if (download)
        {
            download++;

            while (
                *download == ' ' ||
                *download == '\t'
            )
            {
                download++;
            }

            if (strncmp(
                    download,
                    "true",
                    4
                ) == 0)
            {
                device->download = true;
            }
        }
    }

    device->active = true;

    return true;
}


static void add_device(
    const SenderDevice *device)
{
    if (!device)
        return;

    /*
     * Never add ourselves.
     */
    if (strcmp(
            device->fingerprint,
            "LocalS-NX-Switch"
        ) == 0)
    {
        return;
    }

    /*
     * Update an existing device if its fingerprint
     * is already known.
     */
    for (size_t i = 0;
         i < device_count;
         i++)
    {
        if (strcmp(
                devices[i].fingerprint,
                device->fingerprint
            ) == 0)
        {
            devices[i] = *device;
            devices[i].active = true;
            return;
        }
    }

    /*
     * Device list is full.
     */
    if (device_count >=
        SENDER_MAX_DEVICES)
    {
        return;
    }

    devices[device_count] =
        *device;

    devices[device_count].active =
        true;

    device_count++;
}

bool sender_handle_register(
    int sock,
    const char *json,
    const char *ip)
{
    SenderDevice device;

    if (!parse_device(
            &device,
            json,
            ip))
    {
        http_send(
            sock,
            400,
            "Bad Request",
            "text/plain",
            "Invalid LocalSend registration"
        );

        return false;
    }

    add_device(&device);

    printf(
        "Discovered: %s (%s)\n",
        device.alias,
        device.ip
    );

    consoleUpdate(NULL);

    const char *response =
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

    http_send_json(
        sock,
        response
    );

    return true;
}


size_t sender_get_device_count(void)
{
    return device_count;
}


const SenderDevice *sender_get_device(
    size_t index)
{
    if (index >= device_count)
        return NULL;

    return &devices[index];
}


/*
 * ---------------------------------------------------------
 * Discovery UI
 * ---------------------------------------------------------
 */

int sender_choose_device(
    int server)
{
    sender_clear_devices();

    sender_announce();

    PadState pad;

    padInitializeDefault(
        &pad
    );

    size_t cursor = 0;

    uint64_t start =
        armGetSystemTick();

    while (appletMainLoop())
    {
        struct sockaddr_in client_address;

        socklen_t client_length =
            sizeof(client_address);

        int client =
            accept(
                server,
                (struct sockaddr *)&client_address,
                &client_length
            );

        if (client >= 0)
        {
            localsend_handle_client(
                client
            );

            close(client);
        }

        padUpdate(&pad);

        u64 down =
            padGetButtonsDown(&pad);

        if (down &
            HidNpadButton_B)
        {
            return -1;
        }

        if (device_count > 0)
        {
            if (down &
                HidNpadButton_Up)
            {
                if (cursor == 0)
                    cursor =
                        device_count - 1;
                else
                    cursor--;
            }

            if (down &
                HidNpadButton_Down)
            {
                cursor++;

                if (cursor >=
                    device_count)
                {
                    cursor = 0;
                }
            }

            if (down &
                HidNpadButton_A)
            {
                return (int)cursor;
            }
        }

        uint64_t now =
            armGetSystemTick();

        if (now - start >=
            DISCOVERY_TIME_NS)
        {
            break;
        }

        printf("\x1b[2J");
        printf("\x1b[H");

        printf(
            "========================================\n"
        );

        printf(
            "             Send To\n"
        );

        printf(
            "========================================\n\n"
        );

        if (device_count == 0)
        {
            printf(
                "Searching for LocalSend devices...\n\n"
            );

            printf(
                "B: Back\n"
            );
        }
        else
        {
            for (size_t i = 0;
                 i < device_count;
                 i++)
            {
                const SenderDevice *device =
                    &devices[i];

                printf(
                    "%s %s\n",
                    i == cursor ? ">" : " ",
                    device->alias
                );

printf(
    "    %s:%u (%s)\n",
    device->ip,
    device->port,
    device->protocol
);
            }

            printf(
                "\nA: Select   B: Back\n"
            );
        }

        consoleUpdate(NULL);
    }

    return -1;
}


/*
 * ---------------------------------------------------------
 * HTTP client helpers
 * ---------------------------------------------------------
 */

static bool send_all(
    int sock,
    const void *data,
    size_t length)
{
    const uint8_t *bytes =
        (const uint8_t *)data;

    while (length > 0)
    {
        if (transfer_cancel_requested)
            return false;

        ssize_t sent =
            send(
                sock,
                bytes,
                length,
                0
            );

        if (sent <= 0)
            return false;

        bytes += sent;
        length -= (size_t)sent;
    }

    return true;
}


static int connect_device(
    const SenderDevice *device)
{
    int sock =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (sock < 0)
    {
        printf(
            "socket() failed: %d (%s)\n",
            errno,
            strerror(errno)
        );

        consoleUpdate(NULL);
        return -1;
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
        htons(device->port);

    if (inet_aton(
            device->ip,
            &address.sin_addr
        ) == 0)
    {
        printf(
            "Invalid IP address.\n"
        );

        consoleUpdate(NULL);

        close(sock);
        return -1;
    }

    printf(
        "Connecting to %s:%u...\n",
        device->ip,
        device->port
    );

    consoleUpdate(NULL);

    /*
     * connect() does not obey SO_SNDTIMEO.
     *
     * Put the socket into non-blocking mode so we can
     * implement our own connection timeout.
     */

    int original_flags =
        fcntl(
            sock,
            F_GETFL,
            0
        );

    if (original_flags < 0)
    {
        printf(
            "fcntl(F_GETFL) failed: %d (%s)\n",
            errno,
            strerror(errno)
        );

        consoleUpdate(NULL);

        close(sock);
        return -1;
    }

    if (fcntl(
            sock,
            F_SETFL,
            original_flags | O_NONBLOCK
        ) < 0)
    {
        printf(
            "fcntl(O_NONBLOCK) failed: %d (%s)\n",
            errno,
            strerror(errno)
        );

        consoleUpdate(NULL);

        close(sock);
        return -1;
    }

    int result =
        connect(
            sock,
            (struct sockaddr *)&address,
            sizeof(address)
        );

    if (result == 0)
    {
        printf(
            "Connected immediately.\n"
        );

        consoleUpdate(NULL);
    }
    else
    {
        if (errno != EINPROGRESS)
        {
            printf(
                "connect() failed immediately: "
                "%d (%s)\n",
                errno,
                strerror(errno)
            );

            consoleUpdate(NULL);

            close(sock);
            return -1;
        }

        fd_set write_fds;

        FD_ZERO(&write_fds);
        FD_SET(sock, &write_fds);

        struct timeval timeout;

        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        printf(
            "Waiting for connection...\n"
        );

        consoleUpdate(NULL);

        result =
            select(
                sock + 1,
                NULL,
                &write_fds,
                NULL,
                &timeout
            );

        if (result < 0)
        {
            printf(
                "select() failed: %d (%s)\n",
                errno,
                strerror(errno)
            );

            consoleUpdate(NULL);

            close(sock);
            return -1;
        }

        if (result == 0)
        {
            printf(
                "Connection timed out.\n"
            );

            consoleUpdate(NULL);

            close(sock);
            return -1;
        }

        int socket_error = 0;

        socklen_t error_length =
            sizeof(socket_error);

        if (getsockopt(
                sock,
                SOL_SOCKET,
                SO_ERROR,
                &socket_error,
                &error_length
            ) < 0)
        {
            printf(
                "getsockopt() failed: "
                "%d (%s)\n",
                errno,
                strerror(errno)
            );

            consoleUpdate(NULL);

            close(sock);
            return -1;
        }

        printf(
            "SO_ERROR = %d (%s)\n",
            socket_error,
            strerror(socket_error)
        );

        consoleUpdate(NULL);

        if (socket_error != 0)
        {
            close(sock);
            return -1;
        }
    }

    /*
     * Restore normal blocking mode for the HTTP
     * request/response code.
     */

    if (fcntl(
            sock,
            F_SETFL,
            original_flags
        ) < 0)
    {
        printf(
            "Failed to restore socket flags: "
            "%d (%s)\n",
            errno,
            strerror(errno)
        );

        consoleUpdate(NULL);

        close(sock);
        return -1;
    }

    /*
     * HTTP read/write timeout.
     */

    struct timeval io_timeout;

    io_timeout.tv_sec = 30;
    io_timeout.tv_usec = 0;

    setsockopt(
        sock,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &io_timeout,
        sizeof(io_timeout)
    );

    setsockopt(
        sock,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &io_timeout,
        sizeof(io_timeout)
    );

    printf(
        "Connected!\n"
    );

    consoleUpdate(NULL);

    return sock;
}

/*
 * Find a case-insensitive HTTP header.
 *
 * Returns a pointer to the first character after
 * the header name and colon.
 */
static const char *find_http_header(
    const char *response,
    const char *name)
{
    if (!response ||
        !name)
    {
        return NULL;
    }

    const char *p =
        response;

    while (*p)
    {
        if ((p == response ||
             p[-1] == '\n') &&
            strncasecmp(
                p,
                name,
                strlen(name)
            ) == 0)
        {
            const char *after =
                p + strlen(name);

            if (*after == ':')
                return after + 1;
        }

        p++;
    }

    return NULL;
}


/*
 * Read a complete HTTP response.
 *
 * This reads both the headers and the body using
 * Content-Length. The old implementation stopped
 * at \\r\\n\\r\\n, which is only the end of the
 * headers and can occur before the JSON body arrives.
 */


static bool read_http_response(
    int sock,
    char *response,
    size_t response_size,
    int *status)
{
    if (!response ||
        response_size < 2 ||
        !status)
    {
        return false;
    }

    size_t used = 0;

    response[0] = '\0';

    size_t header_length = 0;

    /*
     * Read until the complete HTTP header arrives.
     */
    while (used + 1 <
           response_size)
    {
        ssize_t received =
            recv(
                sock,
                response + used,
                response_size - used - 1,
                0
            );

        if (received <= 0)
            return false;

        used +=
            (size_t)received;

        response[used] =
            '\0';

        char *header_end =
            strstr(
                response,
                "\r\n\r\n"
            );

        if (header_end)
        {
            header_length =
                (size_t)(
                    header_end +
                    4 -
                    response
                );

            break;
        }
    }

    if (header_length == 0)
        return false;

    int parsed_status = 0;

    if (sscanf(
            response,
            "HTTP/%*s %d",
            &parsed_status
        ) != 1)
    {
        return false;
    }

    *status =
        parsed_status;

    /*
     * Find Content-Length.
     */
    size_t content_length = 0;

    const char *content_header =
        find_http_header(
            response,
            "Content-Length"
        );

    if (content_header)
    {
        while (*content_header == ' ' ||
               *content_header == '\t')
        {
            content_header++;
        }

        content_length =
            (size_t)strtoull(
                content_header,
                NULL,
                10
            );
    }

    /*
     * We may already have received part or all
     * of the body in the same recv() as the headers.
     */
    size_t body_received =
        used - header_length;

    /*
     * Read the remaining body.
     */
    while (body_received <
           content_length)
    {
        if (used + 1 >=
            response_size)
        {
            return false;
        }

        size_t remaining =
            response_size -
            used -
            1;

        size_t needed =
            content_length -
            body_received;

        if (remaining > needed)
            remaining = needed;

        ssize_t received =
            recv(
                sock,
                response + used,
                remaining,
                0
            );

        if (received <= 0)
            return false;

        used +=
            (size_t)received;

        body_received +=
            (size_t)received;

        response[used] =
            '\0';
    }

    return true;
}


/*
 * ---------------------------------------------------------
 * SHA-256
 * ---------------------------------------------------------
 */

static bool calculate_sha256(
    const char *path,
    char output[65])
{
    FILE *file =
        fopen(
            path,
            "rb"
        );

    if (!file)
        return false;

    LocalSha256Context sha;

    local_sha256_init(
        &sha
    );

    uint8_t buffer[
        SENDER_BUFFER_SIZE
    ];

    while (true)
    {
        size_t count =
            fread(
                buffer,
                1,
                sizeof(buffer),
                file
            );

        if (count > 0)
        {
            local_sha256_update(
                &sha,
                buffer,
                count
            );
        }

        if (count <
            sizeof(buffer))
        {
            if (ferror(file))
            {
                fclose(file);
                return false;
            }

            break;
        }
    }

    fclose(file);

    uint8_t digest[32];

    local_sha256_final(
        &sha,
        digest
    );

    local_sha256_hex(
        digest,
        output
    );

    return true;
}


/*
 * ---------------------------------------------------------
 * File helpers
 * ---------------------------------------------------------
 */


static const char *local_filename(
    const char *path)
{
    static char output[1024];

    if (!path)
        return "";

    const char *relative =
        path;

    /*
     * Remove sdmc:/ prefix.
     */
    if (strncmp(
            relative,
            "sdmc:/",
            6
        ) == 0)
    {
        relative += 6;
    }

    /*
     * LocalSend fileName must be relative.
     */
    while (*relative == '/')
    {
        relative++;
    }

    snprintf(
        output,
        sizeof(output),
        "%s",
        relative
    );

    return output;
}


static const char *mime_type(
    const char *filename)
{
    const char *dot =
        strrchr(
            filename,
            '.'
        );

    if (!dot)
        return "application/octet-stream";

    dot++;

    if (strcasecmp(dot, "jpg") == 0 ||
        strcasecmp(dot, "jpeg") == 0)
        return "image/jpeg";

    if (strcasecmp(dot, "png") == 0)
        return "image/png";

    if (strcasecmp(dot, "gif") == 0)
        return "image/gif";

    if (strcasecmp(dot, "webp") == 0)
        return "image/webp";

    if (strcasecmp(dot, "mp3") == 0)
        return "audio/mpeg";

    if (strcasecmp(dot, "flac") == 0)
        return "audio/flac";

    if (strcasecmp(dot, "wav") == 0)
        return "audio/wav";

    if (strcasecmp(dot, "ogg") == 0)
        return "audio/ogg";

    if (strcasecmp(dot, "mp4") == 0)
        return "video/mp4";

    if (strcasecmp(dot, "mkv") == 0)
        return "video/x-matroska";

    if (strcasecmp(dot, "txt") == 0)
        return "text/plain";

    if (strcasecmp(dot, "pdf") == 0)
        return "application/pdf";

    if (strcasecmp(dot, "zip") == 0)
        return "application/zip";

    return "application/octet-stream";
}


/*
 * Escape a string for JSON.
 */
static bool json_append_string(
    char *output,
    size_t output_size,
    size_t *position,
    const char *value)
{
    if (!output ||
        !position ||
        !value)
    {
        return false;
    }

    if (*position >= output_size)
        return false;

    output[(*position)++] = '"';

    while (*value)
    {
        unsigned char c =
            (unsigned char)*value++;

        if (c == '"' ||
            c == '\\')
        {
            if (*position + 2 >=
                output_size)
            {
                return false;
            }

            output[(*position)++] =
                '\\';

            output[(*position)++] =
                (char)c;
        }
        else if (c == '\n')
        {
            if (*position + 2 >=
                output_size)
            {
                return false;
            }

            output[(*position)++] =
                '\\';

            output[(*position)++] =
                'n';
        }
        else if (c == '\r')
        {
            if (*position + 2 >=
                output_size)
            {
                return false;
            }

            output[(*position)++] =
                '\\';

            output[(*position)++] =
                'r';
        }
        else if (c == '\t')
        {
            if (*position + 2 >=
                output_size)
            {
                return false;
            }

            output[(*position)++] =
                '\\';

            output[(*position)++] =
                't';
        }
        else if (c < 32)
        {
            return false;
        }
        else
        {
            if (*position + 1 >=
                output_size)
            {
                return false;
            }

            output[(*position)++] =
                (char)c;
        }
    }

    if (*position + 1 >=
        output_size)
    {
        return false;
    }

    output[(*position)++] =
        '"';

    output[*position] =
        '\0';

    return true;
}


/*
 * ---------------------------------------------------------
 * Prepare-upload
 * ---------------------------------------------------------
 */

static bool prepare_upload(
    const SenderDevice *device,
    const char *paths[],
    size_t path_count,
    SenderUploadToken tokens[],
    char *session_id,
    size_t session_id_size)
{
    char *json =
        malloc(
            SENDER_JSON_MAX
        );

    if (!json)
        return false;

    size_t position = 0;

    json[position++] = '{';

    position += snprintf(
        json + position,
        SENDER_JSON_MAX - position,
        "\"info\":{"
        "\"alias\":\"LocalS-NX\","
        "\"version\":\"2.2\","
        "\"deviceModel\":\"Nintendo Switch\","
        "\"deviceType\":\"mobile\","
        "\"fingerprint\":\"LocalS-NX-Switch\","
        "\"port\":53317,"
        "\"protocol\":\"http\","
        "\"download\":false"
        "},"
        "\"files\":{"
    );

    for (size_t i = 0;
         i < path_count;
         i++)
    {
        struct stat st;

        if (stat(
                paths[i],
                &st
            ) < 0 ||
            !S_ISREG(st.st_mode))
        {
            free(json);
            return false;
        }

        char file_id[64];

        snprintf(
            file_id,
            sizeof(file_id),
            "localsnxs_%zu",
            i
        );

        if (i > 0)
        {
            if (position + 1 >=
                SENDER_JSON_MAX)
            {
                free(json);
                return false;
            }

            json[position++] = ',';
        }

        if (!json_append_string(
                json,
                SENDER_JSON_MAX,
                &position,
                file_id))
        {
            free(json);
            return false;
        }

        if (position + 20 >=
            SENDER_JSON_MAX)
        {
            free(json);
            return false;
        }

        json[position++] = ':';
        json[position++] = '{';

        position += snprintf(
            json + position,
            SENDER_JSON_MAX - position,
            "\"id\":"
        );

        if (!json_append_string(
                json,
                SENDER_JSON_MAX,
                &position,
                file_id))
        {
            free(json);
            return false;
        }

        position += snprintf(
            json + position,
            SENDER_JSON_MAX - position,
            ",\"fileName\":"
        );

        const char *filename =
            local_filename(
                paths[i]
            );

        if (!json_append_string(
                json,
                SENDER_JSON_MAX,
                &position,
                filename))
        {
            free(json);
            return false;
        }

        position += snprintf(
            json + position,
            SENDER_JSON_MAX - position,
            ",\"size\":%lld,"
            "\"fileType\":",
            (long long)st.st_size
        );

        if (!json_append_string(
                json,
                SENDER_JSON_MAX,
                &position,
                mime_type(filename)))
        {
            free(json);
            return false;
        }

        char sha256[65];

        printf(
            "\x1b[2J"
            "\x1b[H"
        );

        printf(
            "Preparing upload\n"
            "================\n\n"
        );

        printf(
            "Hashing %zu/%zu:\n"
            "%s\n\n",
            i + 1,
            path_count,
            filename
        );

        printf(
            "SHA-256: calculating...\n"
        );

        consoleUpdate(NULL);

        if (!calculate_sha256(
                paths[i],
                sha256))
        {
            printf(
                "\nSHA-256 calculation FAILED.\n"
            );

            consoleUpdate(NULL);

            free(json);
            return false;
        }

        printf(
            "SHA-256: %s\n\n",
            sha256
        );

        printf(
            "Hash complete.\n"
        );

        consoleUpdate(NULL);

        position += snprintf(
            json + position,
            SENDER_JSON_MAX - position,
            ",\"sha256\":"
        );

        if (!json_append_string(
                json,
                SENDER_JSON_MAX,
                &position,
                sha256))
        {
            free(json);
            return false;
        }

        if (position + 3 >=
            SENDER_JSON_MAX)
        {
            free(json);
            return false;
        }

        json[position++] = '}';
        json[position] = '\0';
    }

    if (position + 3 >=
        SENDER_JSON_MAX)
    {
        free(json);
        return false;
    }

    json[position++] = '}';
    json[position++] = '}';
    json[position] = '\0';

printf(
    "Preparing HTTP request...\n"
);

consoleUpdate(NULL);

printf(
    "Connecting to %s:%u...\n",
    device->ip,
    device->port
);

consoleUpdate(NULL);

int sock =
    connect_device(device);

if (sock < 0)
{
    printf(
        "Connection failed.\n"
    );

    consoleUpdate(NULL);

    free(json);
    return false;
}

printf(
    "Connected!\n"
);

consoleUpdate(NULL);

    if (sock < 0)
    {
        printf(
            "HTTP connection FAILED.\n"
        );

        consoleUpdate(NULL);

        free(json);
        return false;
    }

    printf(
        "HTTP connected.\n"
    );

    consoleUpdate(NULL);

    char request_header[512];

    int header_length =
        snprintf(
            request_header,
            sizeof(request_header),
            "POST /api/localsend/v2/prepare-upload HTTP/1.1\r\n"
            "Host: %s:%u\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            device->ip,
            device->port,
            position
        );

    bool success =
        header_length > 0 &&
        (size_t)header_length <
            sizeof(request_header);

    if (success)
    {
        printf(
            "Sending prepare-upload request...\n"
        );

        consoleUpdate(NULL);

        success =
            send_all(
                sock,
                request_header,
                (size_t)header_length
            );
    }

    if (success)
    {
        success =
            send_all(
                sock,
                json,
                position
            );
    }

    char *response =
        malloc(
            SENDER_JSON_MAX
        );

    if (!response)
    {
        close(sock);
        free(json);
        return false;
    }

    int status = 0;

    if (success)
    {
        printf(
            "Waiting for prepare-upload response...\n"
        );

        consoleUpdate(NULL);

        success =
            read_http_response(
                sock,
                response,
                SENDER_JSON_MAX,
                &status
            );
    }

    close(sock);
    free(json);

    if (!success)
    {
        printf(
            "Failed to read prepare-upload response.\n"
        );

        consoleUpdate(NULL);

        free(response);
        return false;
    }

    printf(
        "prepare-upload response: HTTP %d\n",
        status
    );

    consoleUpdate(NULL);

    if (status != 200)
    {
        free(response);
        return false;
    }

    if (!json_string(
            response,
            "sessionId",
            session_id,
            session_id_size))
    {
        printf(
            "No sessionId in response.\n"
        );

        consoleUpdate(NULL);

        free(response);
        return false;
    }

    /*
     * Parse each file token.
     *
     * The response contains:
     *
     * "files":{
     *   "localsnxs_0":"token",
     *   "localsnxs_1":"token"
     * }
     */
    const char *files =
        strstr(
            response,
            "\"files\""
        );

    if (!files)
    {
        free(response);
        return false;
    }

    const char *files_end =
        strchr(
            files,
            '}'
        );

    if (!files_end)
    {
        free(response);
        return false;
    }

    for (size_t i = 0;
         i < path_count;
         i++)
    {
        snprintf(
            tokens[i].id,
            sizeof(tokens[i].id),
            "localsnxs_%zu",
            i
        );

        const char *id =
            strstr(
                files,
                tokens[i].id
            );

        if (!id ||
            id >= files_end)
        {
            tokens[i].token[0] =
                '\0';

            continue;
        }

        const char *colon =
            strchr(id, ':');

        if (!colon ||
            colon >= files_end)
        {
            tokens[i].token[0] =
                '\0';

            continue;
        }

        const char *quote =
            strchr(colon, '"');

        if (!quote ||
            quote >= files_end)
        {
            tokens[i].token[0] =
                '\0';

            continue;
        }

        quote++;

        const char *end =
            strchr(
                quote,
                '"'
            );

        if (!end ||
            end > files_end)
        {
            tokens[i].token[0] =
                '\0';

            continue;
        }

        size_t length =
            (size_t)(end - quote);

        if (length >=
            sizeof(tokens[i].token))
        {
            tokens[i].token[0] =
                '\0';

            continue;
        }

        memcpy(
            tokens[i].token,
            quote,
            length
        );

        tokens[i].token[length] =
            '\0';
    }

    free(response);

    return true;
}


/*
 * ---------------------------------------------------------
 * Upload one file
 * ---------------------------------------------------------
 */

static bool upload_file(
    const SenderDevice *device,
    const char *path,
    const char *session_id,
    const char *file_id,
    const char *token,
    size_t file_number,
    size_t file_count)
{
    struct stat st;

    if (
        stat(path, &st) < 0 ||
        !S_ISREG(st.st_mode)
    )
    {
        return false;
    }

    FILE *file =
        fopen(
            path,
            "rb"
        );

    if (!file)
        return false;

    int sock =
        connect_device(device);

    if (sock < 0)
    {
        fclose(file);
        return false;
    }

    char request_header[1024];

    int header_length =
        snprintf(
            request_header,
            sizeof(request_header),
            "POST /api/localsend/v2/upload"
            "?sessionId=%s"
            "&fileId=%s"
            "&token=%s HTTP/1.1\r\n"
            "Host: %s:%u\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: %lld\r\n"
            "Connection: close\r\n"
            "\r\n",
            session_id,
            file_id,
            token,
            device->ip,
            device->port,
            (long long)st.st_size
        );

    if (
        header_length <= 0 ||
        (size_t)header_length >=
            sizeof(request_header)
    )
    {
        fclose(file);
        close(sock);
        return false;
    }

    if (
        !send_all(
            sock,
            request_header,
            (size_t)header_length
        )
    )
    {
        fclose(file);
        close(sock);
        return false;
    }

    uint8_t *buffer =
        malloc(
            SENDER_BUFFER_SIZE
        );

    if (!buffer)
    {
        fclose(file);
        close(sock);
        return false;
    }

    uint64_t sent_total = 0;

    transfer_state.status =
        SENDER_TRANSFER_SENDING;

    transfer_state.current_file_index =
        file_number;

    transfer_state.current_file_bytes =
        0;

    transfer_state.current_file_size =
        (uint64_t)st.st_size;

    snprintf(
        transfer_state.current_file,
        sizeof(transfer_state.current_file),
        "%s",
        local_filename(path)
    );

    while (true)
    {
        if (transfer_cancel_requested)
        {
            transfer_state.status =
                SENDER_TRANSFER_CANCELLED;

            free(buffer);
            fclose(file);
            close(sock);

            return false;
        }

        size_t count =
            fread(
                buffer,
                1,
                SENDER_BUFFER_SIZE,
                file
            );

        if (count > 0)
        {
            if (
                !send_all(
                    sock,
                    buffer,
                    count
                )
            )
            {
                free(buffer);
                fclose(file);
                close(sock);

                if (transfer_cancel_requested)
                {
                    transfer_state.status =
                        SENDER_TRANSFER_CANCELLED;
                }

                return false;
            }

            sent_total +=
                (uint64_t)count;

            transfer_state.current_file_bytes =
                sent_total;

            transfer_state.total_bytes_sent +=
                (uint64_t)count;

            if (transfer_cancel_requested)
            {
                transfer_state.status =
                    SENDER_TRANSFER_CANCELLED;

                free(buffer);
                fclose(file);
                close(sock);

                return false;
            }

            uint64_t now =
                armGetSystemTick();

            uint64_t elapsed_ticks =
                now -
                transfer_state.start_time_ns;

            uint64_t frequency =
                armGetSystemTickFreq();

            if (elapsed_ticks > 0)
            {
                transfer_state.speed_bytes_per_second =
                    (
                        transfer_state.total_bytes_sent *
                        frequency
                    ) /
                    elapsed_ticks;

                if (
                    transfer_state.speed_bytes_per_second > 0 &&
                    transfer_state.total_bytes >
                        transfer_state.total_bytes_sent
                )
                {
                    uint64_t remaining =
                        transfer_state.total_bytes -
                        transfer_state.total_bytes_sent;

                    transfer_state.eta_seconds =
                        remaining /
                        transfer_state.speed_bytes_per_second;
                }
            }

            int percent = 100;

            if (st.st_size > 0)
            {
                percent =
                    (int)(
                        (
                            sent_total *
                            100ULL
                        ) /
                        (uint64_t)st.st_size
                    );
            }

            printf(
                "\rFile %zu/%zu: %s [%d%%]",
                file_number,
                file_count,
                local_filename(path),
                percent
            );

            consoleUpdate(NULL);
        }

        if (count < SENDER_BUFFER_SIZE)
        {
            if (ferror(file))
            {
                free(buffer);
                fclose(file);
                close(sock);

                return false;
            }

            break;
        }
    }

    free(buffer);
    fclose(file);

    printf(
        "\nFile upload finished.\n"
    );

    printf(
        "Waiting for LocalSend response...\n"
    );

    consoleUpdate(NULL);

    char response[4096];

    int status = 0;

    bool success =
        read_http_response(
            sock,
            response,
            sizeof(response),
            &status
        );

    close(sock);

    if (!success)
        return false;

    return status == 200;
}

/*
 * ---------------------------------------------------------
 * Send selected files
 * ---------------------------------------------------------
 */

bool sender_send_files(
    const SenderDevice *device,
    const char *paths[],
    size_t path_count)
{
    transfer_set_stage(
        "sender_send_files entered"
    );

    if (!device ||
        !paths ||
        path_count == 0 ||
        path_count > SENDER_MAX_FILES)
    {
        return false;
    }

    uint64_t total_bytes = 0;

    /*
     * Calculate total transfer size.
     */

    transfer_set_stage(
        "Calculating file sizes"
    );

    for (size_t i = 0;
         i < path_count;
         i++)
    {
        struct stat st;

        if (stat(
                paths[i],
                &st
            ) < 0 ||
            !S_ISREG(st.st_mode))
        {
            return false;
        }

        total_bytes +=
            (uint64_t)st.st_size;
    }

    sender_reset_transfer_state();

    transfer_state.status =
        SENDER_TRANSFER_PREPARING;

    transfer_set_stage(
        "Starting transfer"
    );

    if (transfer_cancel_requested)
    {
        transfer_state.status =
            SENDER_TRANSFER_CANCELLED;

        transfer_set_stage(
            "Transfer cancelled"
        );

        return false;
    }

    transfer_state.total_files =
        path_count;

    transfer_state.total_bytes =
        total_bytes;

    transfer_state.start_time_ns =
        armGetSystemTick();

    SenderUploadToken *tokens =
        calloc(
            path_count,
            sizeof(SenderUploadToken)
        );

    if (!tokens)
    {
        transfer_state.status =
            SENDER_TRANSFER_FAILED;

        transfer_set_stage(
            "Failed to allocate memory"
        );

        return false;
    }

    char session_id[128];

    memset(
        session_id,
        0,
        sizeof(session_id)
    );

    transfer_set_stage(
        "Preparing upload"
    );

    if (!prepare_upload(
            device,
            paths,
            path_count,
            tokens,
            session_id,
            sizeof(session_id)))
    {
        free(tokens);

        if (transfer_cancel_requested)
        {
            transfer_state.status =
                SENDER_TRANSFER_CANCELLED;

            transfer_set_stage(
                "Transfer cancelled"
            );
        }
        else
        {
            transfer_state.status =
                SENDER_TRANSFER_FAILED;

            transfer_set_stage(
                "Prepare upload failed"
            );
        }

        return false;
    }

    transfer_set_stage(
        "Upload prepared"
    );

    /*
     * Upload every file in the transfer.
     */

    for (size_t i = 0;
         i < path_count;
         i++)
    {
        if (transfer_cancel_requested)
        {
            transfer_state.status =
                SENDER_TRANSFER_CANCELLED;

            transfer_set_stage(
                "Transfer cancelled"
            );

            free(tokens);

            return false;
        }

        /*
         * A receiver may reject an individual
         * file by simply omitting its token.
         */

        if (tokens[i].token[0] == '\0')
        {
            continue;
        }

        transfer_set_stage(
            "Uploading file"
        );

        if (!upload_file(
                device,
                paths[i],
                session_id,
                tokens[i].id,
                tokens[i].token,
                i + 1,
                path_count))
        {
            free(tokens);

            if (transfer_cancel_requested)
            {
                transfer_state.status =
                    SENDER_TRANSFER_CANCELLED;

                transfer_set_stage(
                    "Transfer cancelled"
                );
            }
            else
            {
                transfer_state.status =
                    SENDER_TRANSFER_FAILED;

                transfer_set_stage(
                    "File upload failed"
                );
            }

            return false;
        }

        if (transfer_cancel_requested)
        {
            transfer_state.status =
                SENDER_TRANSFER_CANCELLED;

            transfer_set_stage(
                "Transfer cancelled"
            );

            free(tokens);

            return false;
        }
    }

    /*
     * All files completed.
     */

    free(tokens);

    transfer_state.status =
        SENDER_TRANSFER_COMPLETE;

    transfer_state.current_file_bytes =
        transfer_state.current_file_size;

    transfer_state.total_bytes_sent =
        transfer_state.total_bytes;

    transfer_state.eta_seconds =
        0;

    transfer_set_stage(
        "Transfer complete"
    );

    printf(
        "\x1b[2J"
        "\x1b[H"
    );

    printf(
        "========================================\n"
    );

    printf(
        "          Transfer complete!\n"
    );

    printf(
        "========================================\n\n"
    );

    printf(
        "%zu file%s sent to:\n\n",
        path_count,
        path_count == 1 ? "" : "s"
    );

    printf(
        "%s\n",
        device->alias
    );

    printf(
        "%s:%u\n\n",
        device->ip,
        device->port
    );

    printf(
        "Press B to return.\n"
    );

    consoleUpdate(NULL);

    return true;
}