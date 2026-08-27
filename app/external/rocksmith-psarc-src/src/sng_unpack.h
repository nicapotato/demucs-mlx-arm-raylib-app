#ifndef RS_SNG_UNPACK_H
#define RS_SNG_UNPACK_H

#include <stddef.h>
#include <stdint.h>

int rs_sng_unpack(const uint8_t *file_data, size_t file_len, int use_mac_key, uint8_t **plain, size_t *plain_len);

#endif
