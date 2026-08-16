#include "aes_ctr.h"
#include "sng_keys.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "sng_unpack.h"

/* expected_len comes from the SNG header (u32 before the zlib stream). Charts can
 * compress far better than any fixed multiple of src_len, so trust the header and
 * only fall back to growing the buffer if it lied. */
static int rs_inflate_zlib(const uint8_t *src, size_t src_len, size_t expected_len, uint8_t **out, size_t *out_len) {
  size_t cap = expected_len ? expected_len : src_len * 8u;
  for (int attempt = 0; attempt < 8; attempt++) {
    uint8_t *buf = (uint8_t *)malloc(cap ? cap : 1);
    if (!buf) {
      return -1;
    }
    uLongf dest_len = (uLongf)cap;
    int st = uncompress(buf, &dest_len, src, (uLong)src_len);
    if (st == Z_OK) {
      *out = buf;
      *out_len = (size_t)dest_len;
      return 0;
    }
    free(buf);
    if (st != Z_BUF_ERROR) {
      return -1;
    }
    cap = cap ? cap * 2u : 1024u;
  }
  return -1;
}

int rs_sng_unpack(const uint8_t *file_data, size_t file_len, int use_mac_key, uint8_t **plain, size_t *plain_len) {
  if (file_len < 24) {
    return -1;
  }
  uint32_t magic = (uint32_t)file_data[0] | ((uint32_t)file_data[1] << 8) | ((uint32_t)file_data[2] << 16) |
                   ((uint32_t)file_data[3] << 24);
  if (magic != 0x4A) {
    return -1;
  }
  const uint8_t *key = use_mac_key ? RS_SNG_KEY_MAC : RS_SNG_KEY_PC;
  uint8_t counter[16];
  memcpy(counter, file_data + 8, 16);
  size_t cipher_len = file_len - 24;
  uint8_t *dec = (uint8_t *)malloc(cipher_len ? cipher_len : 1);
  if (!dec) {
    return -1;
  }
  rs_aes256_ctr_crypt(key, counter, file_data + 24, dec, cipher_len);

  if (cipher_len < 6) {
    free(dec);
    return -1;
  }

  uint32_t u32 = (uint32_t)dec[0] | ((uint32_t)dec[1] << 8) | ((uint32_t)dec[2] << 16) | ((uint32_t)dec[3] << 24);
  uint16_t zmagic = (uint16_t)dec[4] | ((uint16_t)dec[5] << 8);

  if (zmagic == 0x78DA || zmagic == 0xDA78) {
    const uint8_t *zsrc = dec + 4;
    size_t zlen = cipher_len - 4;
    uint8_t *inf = NULL;
    size_t inf_len = 0;
    if (rs_inflate_zlib(zsrc, zlen, (size_t)u32, &inf, &inf_len) != 0) {
      free(dec);
      return -1;
    }
    free(dec);
    *plain = inf;
    *plain_len = inf_len;
    return 0;
  }

  *plain = dec;
  *plain_len = cipher_len;
  return 0;
}
