/* Regression test for rs_temp_file: unique names under rapid reuse, no truncation of live files.
 * The old Windows GetTempFileNameA delete-then-rename scheme could hand out an existing name,
 * silently truncating a wav that was still being played. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rocksmith_psarc.h"

#define N_PATHS 200

static int fail(const char *msg, const char *detail) {
  fprintf(stderr, "rs_temp_file_test: FAIL: %s%s%s\n", msg, detail ? ": " : "", detail ? detail : "");
  return 1;
}

int main(void) {
  char paths[N_PATHS][768];
  char *err = NULL;

  /* 1. Rapid sequential creation must never reuse a name while the older file exists. */
  for (int i = 0; i < N_PATHS; i++) {
    const char *suffix = (i % 2 == 0) ? ".wav" : ".wem";
    if (rs_temp_make_empty_with_suffix(suffix, paths[i], sizeof paths[i], &err) != 0) {
      int rc = fail("rs_temp_make_empty_with_suffix", err ? err : "?");
      free(err);
      return rc;
    }
    for (int j = 0; j < i; j++) {
      if (strcmp(paths[i], paths[j]) == 0) {
        return fail("duplicate temp path", paths[i]);
      }
    }
  }

  /* 2. Creating new temp files must not disturb an existing one that is still open (playing). */
  const char sentinel[] = "RIFFsentinel-wav-data";
  {
    FILE *fp = fopen(paths[0], "wb");
    if (!fp) {
      return fail("open first temp for write", paths[0]);
    }
    fwrite(sentinel, 1, sizeof sentinel, fp);
    fclose(fp);
  }
  FILE *held_open = fopen(paths[0], "rb");
  if (!held_open) {
    return fail("hold first temp open", paths[0]);
  }
  char extra[8][768];
  for (int i = 0; i < 8; i++) {
    if (rs_temp_make_empty_with_suffix(".wav", extra[i], sizeof extra[i], &err) != 0) {
      int rc = fail("make_empty while file held open", err ? err : "?");
      free(err);
      fclose(held_open);
      return rc;
    }
    if (strcmp(extra[i], paths[0]) == 0) {
      fclose(held_open);
      return fail("temp name collided with open file", extra[i]);
    }
  }
  char readback[sizeof sentinel];
  size_t got = fread(readback, 1, sizeof readback, held_open);
  fclose(held_open);
  if (got != sizeof sentinel || memcmp(readback, sentinel, sizeof sentinel) != 0) {
    return fail("open file was truncated/overwritten by temp creation", paths[0]);
  }

  /* 3. rs_temp_write_bytes round-trip. */
  uint8_t blob[4096];
  for (size_t i = 0; i < sizeof blob; i++) {
    blob[i] = (uint8_t)(i * 31u + 7u);
  }
  char blob_path[768];
  if (rs_temp_write_bytes(blob, sizeof blob, ".wem", blob_path, sizeof blob_path, &err) != 0) {
    int rc = fail("rs_temp_write_bytes", err ? err : "?");
    free(err);
    return rc;
  }
  {
    FILE *fp = fopen(blob_path, "rb");
    if (!fp) {
      return fail("open written blob", blob_path);
    }
    uint8_t back[4096];
    size_t n = fread(back, 1, sizeof back, fp);
    int extra_ch = fgetc(fp);
    fclose(fp);
    if (n != sizeof blob || extra_ch != EOF || memcmp(back, blob, sizeof blob) != 0) {
      return fail("blob round-trip mismatch", blob_path);
    }
  }

  for (int i = 0; i < N_PATHS; i++) {
    remove(paths[i]);
  }
  for (int i = 0; i < 8; i++) {
    remove(extra[i]);
  }
  remove(blob_path);

  printf("rs_temp_file_test: ok (%d unique paths, no truncation, blob round-trip)\n", N_PATHS);
  return 0;
}
