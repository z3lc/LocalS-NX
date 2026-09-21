#include "transfer.h"
#include "http.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include <switch.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define TRANSFER_BUFFER_SIZE (64 * 1024)
#define RECEIVE_DIRECTORY "sdmc:/LocalS-NX"

static TransferSession sessions[TRANSFER_MAX];

static TransferReceiveState receive_state;

static volatile bool receive_accept_requested = false;
static volatile bool receive_decline_requested = false;
static volatile bool receive_cancel_requested = false;

/*
 * Once the user accepts an incoming batch, all files belonging
 * to that batch are allowed to upload without asking again.
 */
static volatile bool receive_batch_accepted = false;


/* --------------------------------------------------------- */
/* Utility                                                    */
/* --------------------------------------------------------- */

static void make_id(
    char *output,
    size_t output_size,
    const char *prefix)
{
    uint64_t tick =
        armGetSystemTick();

    snprintf(
        output,
        output_size,
        "%s-%08lx-%08lx",
        prefix,
        (unsigned long)(tick >> 32),
        (unsigned long)(tick & 0xffffffff)
    );
}


static bool json_string_range(
    const char *start,
    const char *end,
    const char *key,
    char *output,
    size_t output_size)
{
    char search[128];

    snprintf(
        search,
        sizeof(search),
        "\"%s\"",
        key
    );

    const char *p = start;

    while (p < end)
    {
        const char *found =
            strstr(
                p,
                search
            );

        if (!found || found >= end)
            return false;

        p =
            found +
            strlen(search);

        while (
            p < end &&
            (
                *p == ' ' ||
                *p == '\t' ||
                *p == '\r' ||
                *p == '\n'
            )
        )
        {
            p++;
        }

        if (p >= end || *p != ':')
            continue;

        p++;

        while (
            p < end &&
            (
                *p == ' ' ||
                *p == '\t' ||
                *p == '\r' ||
                *p == '\n'
            )
        )
        {
            p++;
        }

        if (p >= end || *p != '"')
            return false;

        p++;

        size_t length = 0;

        while (
            p < end &&
            *p != '"' &&
            length + 1 < output_size
        )
        {
            if (
                *p == '\\' &&
                p + 1 < end
            )
            {
                p++;

                switch (*p)
                {
                    case '"':
                        output[length++] = '"';
                        break;

                    case '\\':
                        output[length++] = '\\';
                        break;

                    case '/':
                        output[length++] = '/';
                        break;

                    case 'n':
                        output[length++] = '\n';
                        break;

                    case 'r':
                        output[length++] = '\r';
                        break;

                    case 't':
                        output[length++] = '\t';
                        break;

                    default:
                        output[length++] = *p;
                        break;
                }

                p++;
                continue;
            }

            output[length++] =
                *p++;
        }

        if (p >= end || *p != '"')
            return false;

        output[length] = '\0';

        return true;
    }

    return false;
}


static bool json_u64_range(
    const char *start,
    const char *end,
    const char *key,
    uint64_t *output)
{
    char search[128];

    snprintf(
        search,
        sizeof(search),
        "\"%s\"",
        key
    );

    const char *p = start;

    while (p < end)
    {
        const char *found =
            strstr(
                p,
                search
            );

        if (!found || found >= end)
            return false;

        p =
            found +
            strlen(search);

        while (
            p < end &&
            (
                *p == ' ' ||
                *p == '\t' ||
                *p == '\r' ||
                *p == '\n'
            )
        )
        {
            p++;
        }

        if (p >= end || *p != ':')
            continue;

        p++;

        while (
            p < end &&
            (
                *p == ' ' ||
                *p == '\t' ||
                *p == '\r' ||
                *p == '\n'
            )
        )
        {
            p++;
        }

        if (p >= end)
            return false;

        *output =
            strtoull(
                p,
                NULL,
                10
            );

        return true;
    }

    return false;
}


/* --------------------------------------------------------- */
/* Filename safety                                            */
/* --------------------------------------------------------- */

static bool safe_filename(
    const char *filename)
{
    if (
        !filename ||
        !filename[0]
    )
    {
        return false;
    }

    if (
        filename[0] == '/' ||
        filename[0] == '\\'
    )
    {
        return false;
    }

    const char *p = filename;

    while (*p)
    {
        const char *component_start =
            p;

        while (
            *p &&
            *p != '/' &&
            *p != '\\'
        )
        {
            p++;
        }

        size_t length =
            (size_t)(
                p - component_start
            );

        if (length == 0)
            return false;

        if (
            (
                length == 1 &&
                component_start[0] == '.'
            ) ||
            (
                length == 2 &&
                component_start[0] == '.' &&
                component_start[1] == '.'
            )
        )
        {
            return false;
        }

        for (
            size_t i = 0;
            i < length;
            i++
        )
        {
            unsigned char c =
                (unsigned char)
                    component_start[i];

            if (c < 32)
                return false;
        }

        if (*p)
            p++;
    }

    return true;
}


/* --------------------------------------------------------- */
/* Sessions                                                   */
/* --------------------------------------------------------- */

static TransferSession *new_session(void)
{
    for (
        int i = 0;
        i < TRANSFER_MAX;
        i++
    )
    {
        if (!sessions[i].active)
        {
            memset(
                &sessions[i],
                0,
                sizeof(TransferSession)
            );

            sessions[i].active = true;

            return &sessions[i];
        }
    }

    return NULL;
}


static TransferSession *find_session(
    const char *session_id,
    const char *file_id,
    const char *token)
{
    for (
        int i = 0;
        i < TRANSFER_MAX;
        i++
    )
    {
        TransferSession *session =
            &sessions[i];

        if (!session->active)
            continue;

        if (
            strcmp(
                session->session_id,
                session_id
            ) != 0
        )
        {
            continue;
        }

        if (
            strcmp(
                session->file_id,
                file_id
            ) != 0
        )
        {
            continue;
        }

        if (
            strcmp(
                session->token,
                token
            ) != 0
        )
        {
            continue;
        }

        return session;
    }

    return NULL;
}


/* --------------------------------------------------------- */
/* Receiver state                                             */
/* --------------------------------------------------------- */

static void receive_set_stage(
    const char *stage)
{
    if (!stage)
        return;

    snprintf(
        receive_state.stage,
        sizeof(receive_state.stage),
        "%s",
        stage
    );
}


void transfer_reset_receive_state(void)
{
    memset(
        &receive_state,
        0,
        sizeof(receive_state)
    );

    receive_state.status =
        TRANSFER_RECEIVE_IDLE;

    receive_accept_requested =
        false;

    receive_decline_requested =
        false;

    receive_cancel_requested =
        false;

    receive_batch_accepted =
        false;
}


const TransferReceiveState *
transfer_get_receive_state(void)
{
    return &receive_state;
}


bool transfer_receive_active(void)
{
    return
        receive_state.status ==
            TRANSFER_RECEIVE_WAITING_CONFIRMATION ||
        receive_state.status ==
            TRANSFER_RECEIVE_RECEIVING;
}


void transfer_accept_receive(void)
{
    receive_accept_requested = true;
    receive_batch_accepted = true;
}


void transfer_decline_receive(void)
{
    receive_decline_requested =
        true;
}


void transfer_cancel_receive(void)
{
    receive_cancel_requested =
        true;
}


/* --------------------------------------------------------- */
/* Receiver helpers                                           */
/* --------------------------------------------------------- */

static size_t receive_file_index(
    const TransferSession *target)
{
    if (!target)
        return 0;

    size_t index = 0;

    for (
        int i = 0;
        i < TRANSFER_MAX;
        i++
    )
    {
        /*
         * Only count sessions belonging to
         * the same LocalSend batch.
         */
        if (
            sessions[i].session_id[0] == '\0'
        )
        {
            continue;
        }

        if (
            strcmp(
                sessions[i].session_id,
                target->session_id
            ) != 0
        )
        {
            continue;
        }

        if (&sessions[i] == target)
            return index;

        index++;
    }

    return 0;
}


static bool all_receive_sessions_complete(
    const char *session_id)
{
    bool found = false;

    for (
        int i = 0;
        i < TRANSFER_MAX;
        i++
    )
    {
        if (
            sessions[i].session_id[0] == '\0'
        )
        {
            continue;
        }

        if (
            strcmp(
                sessions[i].session_id,
                session_id
            ) != 0
        )
        {
            continue;
        }

        found = true;

        if (!sessions[i].complete)
            return false;
    }

    return found;
}


static void receive_update_speed(void)
{
    uint64_t now =
        armGetSystemTick();

    uint64_t elapsed_ticks =
        now -
        receive_state.start_time_ns;

    uint64_t frequency =
        armGetSystemTickFreq();

    if (
        elapsed_ticks == 0 ||
        frequency == 0
    )
    {
        return;
    }

    uint64_t elapsed_seconds =
        elapsed_ticks /
        frequency;

    if (elapsed_seconds == 0)
        return;

    receive_state.speed_bytes_per_second =
        receive_state.total_bytes_received /
        elapsed_seconds;

    if (
        receive_state.speed_bytes_per_second >
        0
    )
    {
        uint64_t remaining =
            receive_state.total_bytes -
            receive_state.total_bytes_received;

        receive_state.eta_seconds =
            remaining /
            receive_state.speed_bytes_per_second;
    }
}


/* --------------------------------------------------------- */
/* Init                                                       */
/* --------------------------------------------------------- */

void transfer_init(void)
{
    memset(
        sessions,
        0,
        sizeof(sessions)
    );

    transfer_reset_receive_state();

    mkdir(
        RECEIVE_DIRECTORY,
        0777
    );
}


/* --------------------------------------------------------- */
/* Prepare upload                                             */
/* --------------------------------------------------------- */

bool transfer_prepare(
    int sock,
    const char *json)
{
    const char *files =
        strstr(
            json,
            "\"files\""
        );

    if (!files)
    {
        http_send(
            sock,
            400,
            "Bad Request",
            "text/plain",
            "Missing files"
        );

        return false;
    }

    const char *files_object =
        strchr(
            files,
            '{'
        );

    if (!files_object)
    {
        http_send(
            sock,
            400,
            "Bad Request",
            "text/plain",
            "Invalid files object"
        );

        return false;
    }

    const char *files_end =
        strrchr(
            files_object,
            '}'
        );

    if (
        !files_end ||
        files_end <= files_object
    )
    {
        http_send(
            sock,
            400,
            "Bad Request",
            "text/plain",
            "Invalid files object"
        );

        return false;
    }

    /*
     * Start a fresh receiver state.
     */
    transfer_reset_receive_state();

    char session_id[128];

    make_id(
        session_id,
        sizeof(session_id),
        "session"
    );

    int accepted = 0;

    char response[8192];

    int response_length =
        snprintf(
            response,
            sizeof(response),
            "{\"sessionId\":\"%s\",\"files\":{",
            session_id
        );

    if (
        response_length < 0 ||
        (size_t)response_length >=
            sizeof(response)
    )
    {
        http_send(
            sock,
            500,
            "Internal Server Error",
            "text/plain",
            "Response too large"
        );

        return false;
    }

    const char *p =
        files_object + 1;

    while (
        p < files_end &&
        accepted < TRANSFER_MAX
    )
    {
        while (
            p < files_end &&
            (
                *p == ' ' ||
                *p == '\t' ||
                *p == '\r' ||
                *p == '\n' ||
                *p == ','
            )
        )
        {
            p++;
        }

        if (
            p >= files_end ||
            *p != '"'
        )
        {
            break;
        }

        const char *id_start =
            ++p;

        while (
            p < files_end &&
            *p != '"'
        )
        {
            p++;
        }

        if (p >= files_end)
            break;

        size_t id_length =
            (size_t)(
                p - id_start
            );

        if (
            id_length == 0 ||
            id_length >= 128
        )
        {
            break;
        }

        char file_id[128];

        memcpy(
            file_id,
            id_start,
            id_length
        );

        file_id[id_length] =
            '\0';

        p++;

        while (
            p < files_end &&
            (
                *p == ' ' ||
                *p == '\t' ||
                *p == '\r' ||
                *p == '\n'
            )
        )
        {
            p++;
        }

        if (
            p >= files_end ||
            *p != ':'
        )
        {
            break;
        }

        p++;

        while (
            p < files_end &&
            (
                *p == ' ' ||
                *p == '\t' ||
                *p == '\r' ||
                *p == '\n'
            )
        )
        {
            p++;
        }

        if (
            p >= files_end ||
            *p != '{'
        )
        {
            break;
        }

        const char *object_start =
            p;

        int depth = 0;

        bool in_string = false;
        bool escaped = false;

        while (p < files_end)
        {
            char c = *p;

            if (in_string)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (c == '\\')
                {
                    escaped = true;
                }
                else if (c == '"')
                {
                    in_string = false;
                }
            }
            else
            {
                if (c == '"')
                {
                    in_string = true;
                }
                else if (c == '{')
                {
                    depth++;
                }
                else if (c == '}')
                {
                    depth--;

                    if (depth == 0)
                    {
                        p++;
                        break;
                    }
                }
            }

            p++;
        }

        const char *object_end =
            p;

        TransferSession *session =
            new_session();

        if (!session)
            break;

        snprintf(
            session->session_id,
            sizeof(session->session_id),
            "%s",
            session_id
        );

        snprintf(
            session->file_id,
            sizeof(session->file_id),
            "%s",
            file_id
        );

        if (
            !json_string_range(
                object_start,
                object_end,
                "fileName",
                session->filename,
                sizeof(session->filename)
            )
        )
        {
            session->active = false;
            continue;
        }

        if (
            !safe_filename(
                session->filename
            )
        )
        {
            session->active = false;
            continue;
        }

        if (
            !json_u64_range(
                object_start,
                object_end,
                "size",
                &session->size
            )
        )
        {
            session->active = false;
            continue;
        }

        /*
         * SHA-256 is optional.
         */
        session->sha256[0] =
            '\0';

        json_string_range(
            object_start,
            object_end,
            "sha256",
            session->sha256,
            sizeof(session->sha256)
        );

        make_id(
            session->token,
            sizeof(session->token),
            "token"
        );

        if (accepted > 0)
        {
            size_t used =
                (size_t)response_length;

            response_length +=
                snprintf(
                    response + used,
                    sizeof(response) - used,
                    ","
                );
        }

        size_t used =
            (size_t)response_length;

        response_length +=
            snprintf(
                response + used,
                sizeof(response) - used,
                "\"%s\":\"%s\"",
                session->file_id,
                session->token
            );

        accepted++;
    }

    if (accepted == 0)
    {
        http_send(
            sock,
            400,
            "Bad Request",
            "text/plain",
            "No valid files"
        );

        return false;
    }

    size_t used =
        (size_t)response_length;

    snprintf(
        response + used,
        sizeof(response) - used,
        "}}"
    );

    /*
     * Calculate the complete batch size.
     */
    receive_state.total_files =
        (size_t)accepted;

    receive_state.total_bytes =
        0;

    for (
        int i = 0;
        i < TRANSFER_MAX;
        i++
    )
    {
        if (
            sessions[i].session_id[0] == '\0'
        )
        {
            continue;
        }

        if (
            strcmp(
                sessions[i].session_id,
                session_id
            ) != 0
        )
        {
            continue;
        }

        receive_state.total_bytes +=
            sessions[i].size;
    }

    receive_state.total_bytes_received =
        0;

    receive_state.current_file_index =
        0;

    receive_state.current_file_bytes =
        0;

    receive_state.current_file_size =
        0;

    receive_state.speed_bytes_per_second =
        0;

    receive_state.eta_seconds =
        0;

    receive_state.start_time_ns =
        armGetSystemTick();

    receive_state.status =
        TRANSFER_RECEIVE_WAITING_CONFIRMATION;

    receive_set_stage(
        "Waiting for transfer"
    );

    /*
     * LocalSend expects this response immediately.
     *
     * The actual transfer then follows on the
     * upload connections.
     */
    http_send_json(
        sock,
        response
    );

    return true;
}


/* --------------------------------------------------------- */
/* Directory creation                                         */
/* --------------------------------------------------------- */

static bool create_parent_directories(
    const char *filename)
{
    char path[1024];

    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        RECEIVE_DIRECTORY,
        filename
    );

    char *p =
        path +
        strlen(RECEIVE_DIRECTORY) +
        1;

    while (*p)
    {
        if (*p == '/')
        {
            *p = '\0';

            if (mkdir(path, 0777) < 0)
            {
                struct stat st;

                if (
                    stat(
                        path,
                        &st
                    ) < 0 ||
                    !S_ISDIR(st.st_mode)
                )
                {
                    *p = '/';

                    return false;
                }
            }

            *p = '/';
        }

        p++;
    }

    return true;
}


/* --------------------------------------------------------- */
/* Receive upload                                              */
/* --------------------------------------------------------- */

void transfer_upload(
    int sock,
    const char *path,
    uint64_t content_length)
{
    char query[1024];

    const char *question =
        strchr(
            path,
            '?'
        );

    if (!question)
    {
        http_send(
            sock,
            400,
            "Bad Request",
            "text/plain",
            "Missing query"
        );

        return;
    }

    snprintf(
        query,
        sizeof(query),
        "%s",
        question + 1
    );

    char session_id[128] = "";
    char file_id[128] = "";
    char token[128] = "";

    char *save = NULL;

    char *part =
        strtok_r(
            query,
            "&",
            &save
        );

    while (part)
    {
        char *equals =
            strchr(
                part,
                '='
            );

        if (equals)
        {
            *equals = '\0';

            const char *key =
                part;

            const char *value =
                equals + 1;

            if (
                strcmp(
                    key,
                    "sessionId"
                ) == 0
            )
            {
                snprintf(
                    session_id,
                    sizeof(session_id),
                    "%s",
                    value
                );
            }
            else if (
                strcmp(
                    key,
                    "fileId"
                ) == 0
            )
            {
                snprintf(
                    file_id,
                    sizeof(file_id),
                    "%s",
                    value
                );
            }
            else if (
                strcmp(
                    key,
                    "token"
                ) == 0
            )
            {
                snprintf(
                    token,
                    sizeof(token),
                    "%s",
                    value
                );
            }
        }

        part =
            strtok_r(
                NULL,
                "&",
                &save
            );
    }

    TransferSession *session =
        find_session(
            session_id,
            file_id,
            token
        );

    if (!session)
    {
        http_send(
            sock,
            403,
            "Forbidden",
            "text/plain",
            "Invalid transfer session"
        );

        return;
    }

    if (
        content_length !=
        session->size
    )
    {
        http_send(
            sock,
            400,
            "Bad Request",
            "text/plain",
            "Incorrect file size"
        );

        session->active = false;

        receive_state.status =
            TRANSFER_RECEIVE_FAILED;

        receive_set_stage(
            "Incorrect file size"
        );

        return;
    }

    /*
 * ---------------------------------------------------------------------
 * Wait for the user to accept or decline the incoming transfer.
 *
 * /prepare-upload has already returned to the sender, so the sender
 * will now open this upload connection. We intentionally wait here
 * until the Switch UI makes a decision.
 *
 * This runs on the LocalSend server thread, so the UI/render thread
 * remains completely responsive.
 * ---------------------------------------------------------------------
 */

if (!receive_batch_accepted)
{
    receive_state.status =
        TRANSFER_RECEIVE_WAITING_CONFIRMATION;

    receive_set_stage(
        "Waiting for user confirmation"
    );

    while (
        !receive_accept_requested &&
        !receive_decline_requested &&
        !receive_cancel_requested
    )
    {
        svcSleepThread(
            10000000ULL
        );
    }

    /*
     * User declined the transfer.
     */
    if (
        receive_decline_requested
    )
    {
        session->active = false;

        receive_state.status =
            TRANSFER_RECEIVE_CANCELLED;

        receive_set_stage(
            "Transfer declined"
        );

        http_send(
            sock,
            409,
            "Conflict",
            "text/plain",
            "Transfer declined"
        );

        return;
    }

    /*
     * User cancelled while the confirmation screen
     * was still visible.
     */
    if (
        receive_cancel_requested
    )
    {
        session->active = false;

        receive_state.status =
            TRANSFER_RECEIVE_CANCELLED;

        receive_set_stage(
            "Transfer cancelled"
        );

        http_send(
            sock,
            409,
            "Conflict",
            "text/plain",
            "Transfer cancelled"
        );

        return;
    }

    /*
     * User accepted.
     *
     * receive_batch_accepted is already set by
     * transfer_accept_receive().
     */
}

    /*
     * This is the file currently being received.
     */
    receive_state.status =
        TRANSFER_RECEIVE_RECEIVING;

    receive_state.current_file_index =
        receive_file_index(
            session
        );

    snprintf(
        receive_state.current_file,
        sizeof(receive_state.current_file),
        "%s",
        session->filename
    );

    receive_state.current_file_size =
        session->size;

    receive_state.current_file_bytes =
        0;

    receive_set_stage(
        "Receiving file"
    );

    /*
     * The amount already received before this
     * particular file began.
     */
    uint64_t completed_before =
        receive_state.total_bytes_received;

    char temporary_path[1024];

    char final_path[1024];

    if (
        !create_parent_directories(
            session->filename
        )
    )
    {
        http_send(
            sock,
            500,
            "Internal Server Error",
            "text/plain",
            "Could not create destination directory"
        );

        session->active = false;

        receive_state.status =
            TRANSFER_RECEIVE_FAILED;

        receive_set_stage(
            "Could not create directory"
        );

        return;
    }

    snprintf(
        temporary_path,
        sizeof(temporary_path),
        RECEIVE_DIRECTORY "/.%s.part",
        session->filename
    );

    snprintf(
        final_path,
        sizeof(final_path),
        RECEIVE_DIRECTORY "/%s",
        session->filename
    );

    FILE *file =
        fopen(
            temporary_path,
            "wb"
        );

    if (!file)
    {
        http_send(
            sock,
            500,
            "Internal Server Error",
            "text/plain",
            "Could not open destination"
        );

        session->active = false;

        receive_state.status =
            TRANSFER_RECEIVE_FAILED;

        receive_set_stage(
            "Could not open destination"
        );

        return;
    }

    uint8_t *buffer =
        malloc(
            TRANSFER_BUFFER_SIZE
        );

    if (!buffer)
    {
        fclose(file);

        unlink(
            temporary_path
        );

        http_send(
            sock,
            500,
            "Internal Server Error",
            "text/plain",
            "Out of memory"
        );

        session->active = false;

        receive_state.status =
            TRANSFER_RECEIVE_FAILED;

        receive_set_stage(
            "Out of memory"
        );

        return;
    }

    LocalSha256Context sha256;

    local_sha256_init(
        &sha256
    );

    uint64_t received = 0;

    struct timeval timeout;

    timeout.tv_sec = 30;
    timeout.tv_usec = 0;

    setsockopt(
        sock,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );

    while (
        received <
        session->size
    )
    {
        /*
         * Allow the UI / cancel path to stop
         * the current receive.
         */
        if (
            receive_cancel_requested
        )
        {
            free(buffer);

            fclose(file);

            unlink(
                temporary_path
            );

            session->active =
                false;

            receive_state.status =
                TRANSFER_RECEIVE_CANCELLED;

            receive_set_stage(
                "Transfer cancelled"
            );

            http_send(
                sock,
                409,
                "Conflict",
                "text/plain",
                "Transfer cancelled"
            );

            return;
        }

        uint64_t remaining =
            session->size -
            received;

        size_t wanted =
            TRANSFER_BUFFER_SIZE;

        if (
            remaining <
            wanted
        )
        {
            wanted =
                (size_t)remaining;
        }

        ssize_t result =
            recv(
                sock,
                buffer,
                wanted,
                0
            );

        if (result <= 0)
        {
            free(buffer);

            fclose(file);

            unlink(
                temporary_path
            );

            session->active =
                false;

            receive_state.status =
                TRANSFER_RECEIVE_FAILED;

            receive_set_stage(
                "Transfer connection lost"
            );

            return;
        }

        size_t written =
            fwrite(
                buffer,
                1,
                (size_t)result,
                file
            );

        if (
            written !=
            (size_t)result
        )
        {
            free(buffer);

            fclose(file);

            unlink(
                temporary_path
            );

            session->active =
                false;

            receive_state.status =
                TRANSFER_RECEIVE_FAILED;

            receive_set_stage(
                "SD write failed"
            );

            return;
        }

        local_sha256_update(
            &sha256,
            buffer,
            (size_t)result
        );

        received +=
            (uint64_t)result;

        /*
         * Update live receiver state.
         */
        receive_state.current_file_bytes =
            received;

        receive_state.total_bytes_received =
            completed_before +
            received;

        receive_update_speed();
    }

    fflush(file);

    fclose(file);

    free(buffer);

    receive_state.current_file_bytes =
        session->size;

    receive_state.total_bytes_received =
        completed_before +
        session->size;

    receive_update_speed();

    /*
     * Calculate SHA-256.
     */
    uint8_t digest[32];

    char digest_hex[65];

    local_sha256_final(
        &sha256,
        digest
    );

    local_sha256_hex(
        digest,
        digest_hex
    );

    /*
     * Verify checksum if supplied.
     */
    if (session->sha256[0])
    {
        if (
            strcasecmp(
                digest_hex,
                session->sha256
            ) != 0
        )
        {
            unlink(
                temporary_path
            );

            http_send(
                sock,
                422,
                "Unprocessable Entity",
                "text/plain",
                "SHA-256 checksum mismatch"
            );

            session->active =
                false;

            receive_state.status =
                TRANSFER_RECEIVE_FAILED;

            receive_set_stage(
                "SHA-256 mismatch"
            );

            return;
        }
    }

    /*
     * Only expose the completed file after
     * the entire transfer and checksum pass.
     */
    if (
        rename(
            temporary_path,
            final_path
        ) != 0
    )
    {
        unlink(
            temporary_path
        );

        http_send(
            sock,
            500,
            "Internal Server Error",
            "text/plain",
            "Could not finalize file"
        );

        session->active =
            false;

        receive_state.status =
            TRANSFER_RECEIVE_FAILED;

        receive_set_stage(
            "Could not finalize file"
        );

        return;
    }

    http_send(
        sock,
        200,
        "OK",
        "text/plain",
        ""
    );

    session->active =
        false;

    session->complete =
        true;

    /*
     * If every file belonging to this batch
     * has now completed, finish the receiver.
     */
    if (
        all_receive_sessions_complete(
            session->session_id
        )
    )
    {
        receive_state.status =
            TRANSFER_RECEIVE_COMPLETE;

        receive_state.total_bytes_received =
            receive_state.total_bytes;

        receive_state.current_file_bytes =
            receive_state.current_file_size;

        receive_state.eta_seconds =
            0;

        receive_update_speed();

        receive_set_stage(
            "Transfer complete"
        );
    }
    else
    {
        /*
         * More files from this batch remain.
         *
         * The user has already accepted the batch, so we do
         * not ask for confirmation again.
         */
        receive_state.status =
            TRANSFER_RECEIVE_RECEIVING;

        receive_set_stage(
            "Waiting for next file"
        );
    }
}


/* --------------------------------------------------------- */
/* Cancel                                                     */
/* --------------------------------------------------------- */

void transfer_cancel(
    int sock,
    const char *json)
{
    char session_id[128];

    if (
        !json_string_range(
            json,
            json + strlen(json),
            "sessionId",
            session_id,
            sizeof(session_id)
        )
    )
    {
        http_send(
            sock,
            400,
            "Bad Request",
            "text/plain",
            "Missing sessionId"
        );

        return;
    }

    for (
        int i = 0;
        i < TRANSFER_MAX;
        i++
    )
    {
        if (
            sessions[i].active &&
            strcmp(
                sessions[i].session_id,
                session_id
            ) == 0
        )
        {
            sessions[i].active =
                false;
        }
    }

    receive_cancel_requested =
        true;

    receive_state.status =
        TRANSFER_RECEIVE_CANCELLED;

    receive_set_stage(
        "Transfer cancelled"
    );

    http_send(
        sock,
        200,
        "OK",
        "text/plain",
        ""
    );
}