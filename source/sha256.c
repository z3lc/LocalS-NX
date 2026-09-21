#include "sha256.h"

#include <string.h>


/*
 * ---------------------------------------------------------
 * Initialize
 * ---------------------------------------------------------
 */

void local_sha256_init(
    LocalSha256Context *ctx)
{
    if (!ctx)
        return;

    sha256ContextCreate(
        &ctx->context
    );
}


/*
 * ---------------------------------------------------------
 * Update
 * ---------------------------------------------------------
 */

void local_sha256_update(
    LocalSha256Context *ctx,
    const void *data,
    size_t length)
{
    if (!ctx ||
        !data ||
        length == 0)
    {
        return;
    }

    sha256ContextUpdate(
        &ctx->context,
        data,
        length
    );
}


/*
 * ---------------------------------------------------------
 * Finalize
 * ---------------------------------------------------------
 */

void local_sha256_final(
    LocalSha256Context *ctx,
    uint8_t output[32])
{
    if (!ctx ||
        !output)
    {
        return;
    }

    sha256ContextGetHash(
        &ctx->context,
        output
    );
}


/*
 * ---------------------------------------------------------
 * Hexadecimal conversion
 * ---------------------------------------------------------
 */

void local_sha256_hex(
    const uint8_t hash[32],
    char output[65])
{
    static const char hex[] =
        "0123456789abcdef";

    if (!hash ||
        !output)
    {
        return;
    }

    for (int i = 0; i < 32; i++)
    {
        output[i * 2] =
            hex[hash[i] >> 4];

        output[i * 2 + 1] =
            hex[hash[i] & 0x0f];
    }

    output[64] = '\0';
}