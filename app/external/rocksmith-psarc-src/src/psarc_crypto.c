#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Vendored tiny-AES-c (third_party/tiny-aes). AES256=1 + ECB=1 are set as target
 * compile definitions in CMakeLists.txt so AES_ctx is sized for 256-bit keys. */
#include "aes.h"

static const unsigned char k_psarc_key[32] = {
    0xC5, 0x3D, 0xB2, 0x38, 0x70, 0xA1, 0xA2, 0xF7, 0x1C, 0xAE, 0x64, 0x06, 0x1F, 0xDD, 0x0E, 0x11, 0x57,
    0x30, 0x9D, 0xC8, 0x52, 0x04, 0xD4, 0xC5, 0xBF, 0xDF, 0x25, 0x09, 0x0D, 0xF2, 0x57, 0x2C,
};

/* Rocksmith 2014 official PC PSARC TOC IV (community key, matches slopsmith ARC_IV). */
static const unsigned char k_psarc_iv[16] = {
    0xE9, 0x15, 0xAA, 0x01, 0x8F, 0xEF, 0x71, 0xFC, 0x50, 0x81, 0x32, 0xE4, 0xBB, 0x4C, 0xEB, 0x42,
};

/* AES-256-CFB128 decrypt (matches OpenSSL EVP_aes_256_cfb128 with padding off):
 * keystream = E(shift_register); plain = cipher XOR keystream; next shift_register = cipher block.
 * The final block may be partial. */
int rs_psarc_decrypt_toc_region(const uint8_t *cipher, size_t cipher_len, uint8_t *plain_out) {
  struct AES_ctx ctx;
  AES_init_ctx(&ctx, k_psarc_key);

  uint8_t shift_reg[16];
  memcpy(shift_reg, k_psarc_iv, sizeof shift_reg);

  size_t off = 0;
  while (off < cipher_len) {
    uint8_t keystream[16];
    memcpy(keystream, shift_reg, 16);
    AES_ECB_encrypt(&ctx, keystream); /* in-place block encrypt */

    size_t n = cipher_len - off;
    if (n > 16) {
      n = 16;
    }
    /* Save the cipher block before writing plain_out (cipher may alias plain_out). */
    memcpy(shift_reg, cipher + off, n);
    for (size_t i = 0; i < n; i++) {
      plain_out[off + i] = cipher[off + i] ^ keystream[i];
    }
    off += n;
  }
  return 0;
}
