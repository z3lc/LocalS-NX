#ifndef TRANSFER_H
#define TRANSFER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define TRANSFER_MAX 8

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool active;
    bool complete;

    char session_id[128];
    char file_id[128];
    char token[128];

    char filename[512];

    uint64_t size;
    char sha256[65];
} TransferSession;


/* --------------------------------------------------------- */
/* Receiver UI state                                          */
/* --------------------------------------------------------- */

typedef enum
{
    TRANSFER_RECEIVE_IDLE = 0,
    TRANSFER_RECEIVE_WAITING_CONFIRMATION,
    TRANSFER_RECEIVE_RECEIVING,
    TRANSFER_RECEIVE_COMPLETE,
    TRANSFER_RECEIVE_FAILED,
    TRANSFER_RECEIVE_CANCELLED
} TransferReceiveStatus;


typedef struct
{
    TransferReceiveStatus status;

    char current_file[512];

    size_t current_file_index;
    size_t total_files;

    uint64_t current_file_bytes;
    uint64_t current_file_size;

    uint64_t total_bytes;
    uint64_t total_bytes_received;

    uint64_t start_time_ns;
    uint64_t speed_bytes_per_second;
    uint64_t eta_seconds;

    char stage[128];
} TransferReceiveState;


/* --------------------------------------------------------- */
/* Receiver                                                   */
/* --------------------------------------------------------- */

void transfer_init(void);

bool transfer_prepare(
    int sock,
    const char *json
);

void transfer_upload(
    int sock,
    const char *path,
    uint64_t content_length
);

void transfer_cancel(
    int sock,
    const char *json
);


/* --------------------------------------------------------- */
/* Receiver UI control                                        */
/* --------------------------------------------------------- */

void transfer_reset_receive_state(void);

const TransferReceiveState *
transfer_get_receive_state(void);

bool transfer_receive_active(void);

void transfer_accept_receive(void);

void transfer_decline_receive(void);

void transfer_cancel_receive(void);

#ifdef __cplusplus
}
#endif

#endif