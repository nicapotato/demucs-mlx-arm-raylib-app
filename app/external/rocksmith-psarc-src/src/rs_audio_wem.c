/* Decode Wwise .wem (RIFF WAVE Vorbis) to PCM WAV via bundled vgmstream (libvorbis).
 *
 * Bytes-based decode feeds vgmstream from memory (custom libstreamfile_t); no intermediate
 * temp .wem touches disk. File-based decode supports cooperative cancellation so a queued
 * background decode can be abandoned quickly when the user switches songs. */
#include "rocksmith_psarc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libvgmstream.h"
#include "libvgmstream_streamfile.h"
#include "wav_utils.h"

/* ---- memory-backed libstreamfile ----
 * vgmstream reopens streamfiles internally via sf->open(), so clones share the caller's buffer;
 * the buffer must outlive the decode (guaranteed: decode is synchronous within the call). */

#define RS_MEM_WEM_NAME "rsaudio_mem.wem" /* extension drives vgmstream format detection */

typedef struct {
  const uint8_t *data;
  int64_t len;
} RsMemSf;

static libstreamfile_t *rs_memsf_create(const uint8_t *data, int64_t len);

static int rs_memsf_read(void *user_data, uint8_t *dst, int64_t offset, int length) {
  RsMemSf *m = (RsMemSf *)user_data;
  if (!m || !dst || length <= 0 || offset < 0 || offset >= m->len) {
    return 0;
  }
  int64_t avail = m->len - offset;
  if ((int64_t)length > avail) {
    length = (int)avail;
  }
  memcpy(dst, m->data + offset, (size_t)length);
  return length;
}

static int64_t rs_memsf_get_size(void *user_data) {
  RsMemSf *m = (RsMemSf *)user_data;
  return m ? m->len : 0;
}

static const char *rs_memsf_get_name(void *user_data) {
  (void)user_data;
  return RS_MEM_WEM_NAME;
}

static libstreamfile_t *rs_memsf_open(void *user_data, const char *filename) {
  RsMemSf *m = (RsMemSf *)user_data;
  /* Only reopens of ourselves are possible; there are no companion files in memory. */
  if (!m || !filename || strcmp(filename, RS_MEM_WEM_NAME) != 0) {
    return NULL;
  }
  return rs_memsf_create(m->data, m->len);
}

static void rs_memsf_close(libstreamfile_t *libsf) {
  if (!libsf) {
    return;
  }
  free(libsf->user_data);
  free(libsf);
}

static libstreamfile_t *rs_memsf_create(const uint8_t *data, int64_t len) {
  RsMemSf *m = (RsMemSf *)calloc(1, sizeof(RsMemSf));
  libstreamfile_t *sf = (libstreamfile_t *)calloc(1, sizeof(libstreamfile_t));
  if (!m || !sf) {
    free(m);
    free(sf);
    return NULL;
  }
  m->data = data;
  m->len = len;
  sf->user_data = m;
  sf->read = rs_memsf_read;
  sf->get_size = rs_memsf_get_size;
  sf->get_name = rs_memsf_get_name;
  sf->open = rs_memsf_open;
  sf->close = rs_memsf_close;
  return sf;
}

/* ---- decode core ----
 * Returns 0 ok, 1 cancelled, -1 error. Does not close sf; on non-zero the wav file content is
 * undefined and the caller should delete it. */
static int decode_sf_to_wav_file(libstreamfile_t *sf, const char *wav_path, const volatile int *cancel_flag,
                                 char **errmsg) {
  libvgmstream_set_log(LIBVGMSTREAM_LOG_LEVEL_NONE, NULL);

  libvgmstream_config_t vcfg = {0};
  vcfg.ignore_loop = true;
  vcfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;

  libvgmstream_t *vgmstream = libvgmstream_create(sf, 0, &vcfg);
  if (!vgmstream) {
    if (errmsg) {
      *errmsg = strdup("libvgmstream_create failed (unsupported .wem?)");
    }
    return -1;
  }

  int64_t play_samples = vgmstream->format->play_samples;
  if (play_samples <= 0) {
    if (errmsg) {
      *errmsg = strdup("vgmstream: invalid play_samples");
    }
    libvgmstream_free(vgmstream);
    return -1;
  }

  FILE *outfile = fopen(wav_path, "wb");
  if (!outfile) {
    if (errmsg) {
      *errmsg = strdup("cannot open output wav");
    }
    libvgmstream_free(vgmstream);
    return -1;
  }

  uint8_t wav_buf[0x100];
  wav_header_t wav = {
      .sample_count = (int32_t)play_samples,
      .sample_rate = vgmstream->format->sample_rate,
      .channels = vgmstream->format->channels,
      .write_smpl_chunk = false,
      .sample_size = vgmstream->format->sample_size,
      .is_float = vgmstream->format->sample_format == LIBVGMSTREAM_SFMT_FLOAT,
  };

  size_t hdr_bytes = wav_make_header(wav_buf, sizeof(wav_buf), &wav);
  if (hdr_bytes == 0) {
    if (errmsg) {
      *errmsg = strdup("wav_make_header failed");
    }
    fclose(outfile);
    libvgmstream_free(vgmstream);
    return -1;
  }
  if (fwrite(wav_buf, 1, hdr_bytes, outfile) != hdr_bytes) {
    if (errmsg) {
      *errmsg = strdup("write wav header failed");
    }
    fclose(outfile);
    libvgmstream_free(vgmstream);
    return -1;
  }

  while (!vgmstream->decoder->done) {
    if (cancel_flag && *cancel_flag) {
      fclose(outfile);
      libvgmstream_free(vgmstream);
      return 1;
    }
    int err = libvgmstream_render(vgmstream);
    if (err < 0) {
      break;
    }
    void *buf = vgmstream->decoder->buf;
    int buf_bytes = vgmstream->decoder->buf_bytes;
    int sample_size = vgmstream->format->sample_size;
    wav_swap_samples_le(buf, vgmstream->format->channels * vgmstream->decoder->buf_samples, sample_size);
    if (fwrite(buf, 1, (size_t)buf_bytes, outfile) != (size_t)buf_bytes) {
      if (errmsg) {
        *errmsg = strdup("write pcm failed");
      }
      fclose(outfile);
      libvgmstream_free(vgmstream);
      return -1;
    }
  }

  fclose(outfile);
  libvgmstream_free(vgmstream);
  return 0;
}

/* Shared tail: make temp wav, decode sf into it, clean up on failure/cancel. */
static int decode_sf_to_temp_wav(libstreamfile_t *sf, char *wav_path_out, size_t wav_path_out_sz,
                                 const volatile int *cancel_flag, char **errmsg) {
  char wav_tmp[768];
  char *e2 = NULL;
  if (rs_temp_make_empty_with_suffix(".wav", wav_tmp, sizeof(wav_tmp), &e2) != 0) {
    if (errmsg) {
      *errmsg = e2 ? e2 : strdup("temp .wav path failed");
      e2 = NULL;
    }
    free(e2);
    return -1;
  }
  free(e2);

  int dec = decode_sf_to_wav_file(sf, wav_tmp, cancel_flag, errmsg);
  if (dec != 0) {
    remove(wav_tmp);
    return dec;
  }
  snprintf(wav_path_out, wav_path_out_sz, "%s", wav_tmp);
  return 0;
}

int rs_audio_wem_decode_bytes_to_temp_wav(const uint8_t *wem_data, size_t wem_len, char *wav_path_out,
                                          size_t wav_path_out_sz, char **errmsg) {
  if (errmsg) {
    *errmsg = NULL;
  }
  if (!wem_data || wem_len == 0 || !wav_path_out || wav_path_out_sz < 8) {
    if (errmsg) {
      *errmsg = strdup("invalid arguments");
    }
    return -1;
  }

  libstreamfile_t *sf = rs_memsf_create(wem_data, (int64_t)wem_len);
  if (!sf) {
    if (errmsg) {
      *errmsg = strdup("memory streamfile alloc failed");
    }
    return -1;
  }
  int dec = decode_sf_to_temp_wav(sf, wav_path_out, wav_path_out_sz, NULL, errmsg);
  libstreamfile_close(sf);
  return dec == 0 ? 0 : -1;
}

int rs_audio_wem_decode_file_to_temp_wav_cancellable(const char *wem_path, char *wav_path_out, size_t wav_path_out_sz,
                                                     const volatile int *cancel_flag, char **errmsg) {
  if (errmsg) {
    *errmsg = NULL;
  }
  if (!wem_path || !wem_path[0] || !wav_path_out || wav_path_out_sz < 8) {
    if (errmsg) {
      *errmsg = strdup("invalid arguments");
    }
    return -1;
  }

  libstreamfile_t *sf = libstreamfile_open_from_stdio(wem_path);
  if (!sf) {
    if (errmsg) {
      *errmsg = strdup("libstreamfile_open_from_stdio failed");
    }
    return -1;
  }
  int dec = decode_sf_to_temp_wav(sf, wav_path_out, wav_path_out_sz, cancel_flag, errmsg);
  libstreamfile_close(sf);
  return dec;
}

int rs_audio_wem_decode_file_to_temp_wav(const char *wem_path, char *wav_path_out, size_t wav_path_out_sz,
                                         char **errmsg) {
  int rc = rs_audio_wem_decode_file_to_temp_wav_cancellable(wem_path, wav_path_out, wav_path_out_sz, NULL, errmsg);
  return rc == 0 ? 0 : -1;
}
