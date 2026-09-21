#ifndef LOCALSNX_LOCALSEND_H
#define LOCALSNX_LOCALSEND_H

#include <stdbool.h>

#define LOCALSEND_PORT 53317
#define LOCALSEND_MULTICAST "224.0.0.167"

#ifdef __cplusplus
extern "C" {
#endif

void localsend_init(void);

bool localsend_start_server(void);

void localsend_poll(void);
void localsend_stop_server(void);

void localsend_discovery_poll(void);

void localsend_stop_server(void);

void localsend_announce(void);

void localsend_handle_client(
    int client
);

#ifdef __cplusplus
}
#endif

#endif