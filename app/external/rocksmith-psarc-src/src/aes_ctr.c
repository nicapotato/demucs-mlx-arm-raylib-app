#include "aes_ctr.h"

/* Vendored tiny-AES-c (third_party/tiny-aes). AES256=1 + ECB=1 are set as target
 * compile definitions in CMakeLists.txt so AES_ctx is sized for 256-bit keys. */
#include "aes.h"

#include <string.h>

void rs_aes256_ctr_crypt(const uint8_t key[32], uint8_t counter[16], const uint8_t *in, uint8_t *out, size_t len) {
  struct AES_ctx ctx;
  AES_init_ctx(&ctx, key);
  size_t off = 0;
  uint8_t keystream[16];
  size_t kpos = 16;
  while (off < len) {
    if (kpos == 16) {
      memcpy(keystream, counter, 16);
      AES_ECB_encrypt(&ctx, keystream); /* in-place block encrypt */
      for (int j = 15; j >= 0; j--) {
        if (++counter[j] != 0) {
          break;
        }
      }
      kpos = 0;
    }
    out[off] = in[off] ^ keystream[kpos++];
    off++;
  }
}
