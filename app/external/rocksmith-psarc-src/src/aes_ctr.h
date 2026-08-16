#ifndef RS_AES_CTR_H
#define RS_AES_CTR_H

#include <stddef.h>
#include <stdint.h>

void rs_aes256_ctr_crypt(const uint8_t key[32], uint8_t counter[16], const uint8_t *in, uint8_t *out, size_t len);

#endif
