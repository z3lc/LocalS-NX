#ifndef LOCAL_SHA256_H
#define LOCAL_SHA256_H

#include <stddef.h>
#include <stdint.h>

#include <switch.h>

/*
 * Wrapper around libnx's hardware-accelerated SHA-256.
 *
 * We use our own type name so it doesn't collide with
 * libnx's Sha256Context.
 */
typedef struct
{
    Sha256Context context;

} LocalSha256Context;


/*
 * Initialize SHA-256.
 */
void local_sha256_init(
    LocalSha256Context *ctx
);


/*
 * Add data to the SHA-256 calculation.
 */
void local_sha256_update(
    LocalSha256Context *ctx,
    const void *data,
    size_t length
);


/*
 * Finish SHA-256 and write the 32-byte digest.
 */
void local_sha256_final(
    LocalSha256Context *ctx,
    uint8_t output[32]
);


/*
 * Convert a 32-byte digest to a 64-character
 * lowercase hexadecimal string.
 *
 * output must have room for 65 bytes including
 * the terminating NUL.
 */
void local_sha256_hex(
    const uint8_t hash[32],
    char output[65]
);

#endif