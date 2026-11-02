#ifndef AES_H
#define AES_H

#include <stdint.h>

#define AES_BLOCK_SIZE  16
#define AES_KEY_SIZE    32   // AES-256

typedef struct {
    uint8_t round_key[240];
    uint8_t iv[AES_BLOCK_SIZE];
} AES_ctx;

void AES_init_ctx_iv(AES_ctx *ctx, const uint8_t *key, const uint8_t *iv);
void AES_CBC_decrypt_buffer(AES_ctx *ctx, uint8_t *buf, uint32_t length);

#endif /* AES_H */
