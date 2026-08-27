#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rocksmith_psarc.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.wem>\n", argv[0]);
    return 1;
  }
  FILE *fp = fopen(argv[1], "rb");
  if (!fp) {
    perror("fopen");
    return 1;
  }
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (sz <= 0) {
    fprintf(stderr, "empty file\n");
    fclose(fp);
    return 1;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) {
    fclose(fp);
    return 1;
  }
  if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
    fprintf(stderr, "read failed\n");
    free(buf);
    fclose(fp);
    return 1;
  }
  fclose(fp);

  char wav_path[768];
  char *err = NULL;
  int r = rs_audio_wem_decode_bytes_to_temp_wav(buf, (size_t)sz, wav_path, sizeof(wav_path), &err);
  free(buf);
  if (r != 0) {
    fprintf(stderr, "decode failed: %s\n", err ? err : "?");
    free(err);
    return 1;
  }
  free(err);

  FILE *check = fopen(wav_path, "rb");
  if (!check) {
    perror("open wav");
    return 1;
  }
  fseek(check, 0, SEEK_END);
  long wz = ftell(check);
  fclose(check);
  printf("rs_wem_smoke: ok wav_path=%s size=%ld\n", wav_path, wz);
#ifndef _WIN32
  if (!getenv("RS_WEM_SMOKE_KEEP")) {
    remove(wav_path);
  }
#else
  if (!getenv("RS_WEM_SMOKE_KEEP")) {
    remove(wav_path);
  }
#endif
  return 0;
}
