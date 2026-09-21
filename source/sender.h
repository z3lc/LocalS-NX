#ifndef SENDER_H
#define SENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SENDER_MAX_DEVICES 16

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool active;

    char alias[128];
    char version[32];
    char device_model[128];
    char device_type[32];

    char fingerprint[128];

    char ip[64];
    uint16_t port;

    char protocol[16];

    bool download;
} SenderDevice;


/*
 * ---------------------------------------------------------
 * Transfer state
 * ---------------------------------------------------------
 */

typedef enum
{
    SENDER_TRANSFER_IDLE = 0,
    SENDER_TRANSFER_PREPARING,
    SENDER_TRANSFER_SENDING,
    SENDER_TRANSFER_COMPLETE,
    SENDER_TRANSFER_FAILED,
    SENDER_TRANSFER_CANCELLED

} SenderTransferStatus;


typedef struct
{
    SenderTransferStatus status;

    char stage[128];

    char current_file[1024];

    size_t current_file_index;
    size_t total_files;

    uint64_t current_file_bytes;
    uint64_t current_file_size;

    uint64_t total_bytes;
    uint64_t total_bytes_sent;

    uint64_t start_time_ns;

    uint64_t speed_bytes_per_second;

    uint64_t eta_seconds;

} SenderTransferState;


void sender_init(void);

void sender_clear_devices(void);

void sender_announce(void);

void sender_discovery_start(void);
void sender_discovery_poll(void);
void sender_discovery_stop(void);
size_t sender_get_device_count(void);

bool sender_handle_register(
    int sock,
    const char *json,
    const char *ip
);

size_t sender_get_device_count(void);

const SenderDevice *sender_get_device(
    size_t index
);

int sender_choose_device(
    int server
);

bool sender_send_files(
    const SenderDevice *device,
    const char *paths[],
    size_t path_count
);


/*
 * Transfer state access.
 */
const SenderTransferState *sender_get_transfer_state(void);

void sender_reset_transfer_state(void);

bool sender_start_transfer(
    const SenderDevice *device,
    const char *paths[],
    size_t path_count
);

bool sender_transfer_running(void);
void sender_wait_for_transfer(void);
void sender_cancel_transfer(void);
bool sender_transfer_cancel_requested(void);

#ifdef __cplusplus
}
#endif

#endif