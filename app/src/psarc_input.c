#include "psarc_input.h"

#include "rocksmith_psarc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dmx_psarc_extract_audio(const char *psarc_path, char *wav_out, size_t wav_out_sz, char **errmsg) {
  if (errmsg) {
    *errmsg = NULL;
  }
  if (!psarc_path || !wav_out || wav_out_sz == 0) {
    if (errmsg) {
      *errmsg = strdup("invalid args");
    }
    return -1;
  }

  char *open_err = NULL;
  RsPsarc *p = rs_psarc_open(psarc_path, &open_err);
  if (!p) {
    if (errmsg) {
      *errmsg = open_err ? open_err : strdup("rs_psarc_open failed");
    } else {
      free(open_err);
    }
    return -1;
  }

  char wem_entry[1024];
  if (rs_psarc_pick_wem_path(p, wem_entry, sizeof wem_entry) != 0) {
    rs_psarc_close(p);
    if (errmsg) {
      *errmsg = strdup("no .wem audio found in psarc");
    }
    return -1;
  }

  uint8_t *bytes = NULL;
  size_t len = 0;
  char *read_err = NULL;
  if (rs_psarc_read_file(p, wem_entry, &bytes, &len, &read_err) != 0) {
    rs_psarc_close(p);
    if (errmsg) {
      *errmsg = read_err ? read_err : strdup("failed to read wem");
    } else {
      free(read_err);
    }
    return -1;
  }
  rs_psarc_close(p);

  char *dec_err = NULL;
  int rc = rs_audio_wem_decode_bytes_to_temp_wav(bytes, len, wav_out, wav_out_sz, &dec_err);
  free(bytes);
  if (rc != 0) {
    if (errmsg) {
      *errmsg = dec_err ? dec_err : strdup("wem decode failed");
    } else {
      free(dec_err);
    }
    return -1;
  }
  return 0;
}
