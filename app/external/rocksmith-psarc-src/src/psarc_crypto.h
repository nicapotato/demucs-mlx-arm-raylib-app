#ifndef RS_PSARC_CRYPTO_H
#define RS_PSARC_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

int rs_psarc_decrypt_toc_region(const uint8_t *cipher, size_t cipher_len, uint8_t *plain_out);

#endif
