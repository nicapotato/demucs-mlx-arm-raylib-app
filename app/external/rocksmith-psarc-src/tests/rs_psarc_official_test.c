#include "rocksmith_psarc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ends_with(const char *s, const char *suffix) {
  size_t ls = strlen(s);
  size_t lx = strlen(suffix);
  if (ls < lx) {
    return 0;
  }
  return strcmp(s + ls - lx, suffix) == 0;
}

static int contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static int is_lead_sng(const char *nm) {
  return ends_with(nm, ".sng") && contains(nm, "lead") &&
         (contains(nm, "songs/bin") || contains(nm, "songs\\bin"));
}

static int test_encrypted_open_parse(const char *path) {
  uint32_t flags = 0;
  if (rs_psarc_peek_archive_flags(path, &flags) != 0) {
    fprintf(stderr, "peek flags failed: %s\n", path);
    return 1;
  }
  if (flags != 4u) {
    fprintf(stderr, "expected archive_flags=4 for encrypted PSARC: %s (got %u)\n", path, flags);
    return 1;
  }

  char *err = NULL;
  RsPsarc *p = rs_psarc_open(path, &err);
  if (!p) {
    fprintf(stderr, "open %s: %s\n", path, err ? err : "?");
    free(err);
    return 1;
  }

  const char *lead = NULL;
  size_t n = rs_psarc_file_count(p);
  for (size_t i = 0; i < n; i++) {
    const char *nm = rs_psarc_file_name(p, i);
    if (nm && is_lead_sng(nm)) {
      lead = nm;
      break;
    }
  }
  if (!lead) {
    fprintf(stderr, "no lead SNG in %s\n", path);
    rs_psarc_close(p);
    return 1;
  }

  uint8_t *buf = NULL;
  size_t len = 0;
  int rc = 1;
  if (rs_psarc_read_file(p, lead, &buf, &len, &err) != 0) {
    fprintf(stderr, "read lead %s: %s\n", lead, err ? err : "?");
    free(err);
    rs_psarc_close(p);
    return 1;
  }

  RsChart ch = {0};
  if (rs_chart_parse_instrument_sng(buf, len, RS_SNG_PLATFORM_PC, &ch, &err) != 0) {
    fprintf(stderr, "parse lead: %s\n", err ? err : "?");
    free(err);
    free(buf);
    rs_chart_free(&ch);
    rs_psarc_close(p);
    return 1;
  }
  if (ch.note_count == 0) {
    fprintf(stderr, "lead note_count=0 for %s\n", path);
  } else {
    rc = 0;
    printf("encrypted PSARC OK: %s lead notes=%zu\n", path, ch.note_count);
  }

  char wem_path[2048];
  if (rs_psarc_pick_wem_path(p, wem_path, sizeof wem_path) != 0) {
    fprintf(stderr, "no wem path in %s\n", path);
    rc = 1;
  } else if (strncmp(wem_path, "audio/", 6) != 0) {
    fprintf(stderr, "unexpected wem path %s in %s\n", wem_path, path);
    rc = 1;
  }

  rs_chart_free(&ch);
  free(buf);
  rs_psarc_close(p);
  return rc;
}

static int test_classify(const char *path, int expect) {
  int got = rs_psarc_classify_source(path);
  if (got != expect) {
    fprintf(stderr, "classify %s: expected %d got %d\n", path, expect, got);
    return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: rs_psarc_official_test <encrypted.psarc> [cdlc.psarc] [official_classify.psarc]\n");
    return 2;
  }
  int rc = test_encrypted_open_parse(argv[1]);
  if (argc >= 3) {
    rc |= test_classify(argv[2], RNR_PSARC_SOURCE_CDLC);
  }
  if (argc >= 4) {
    rc |= test_classify(argv[3], RNR_PSARC_SOURCE_OFFICIAL);
  }
  return rc ? 1 : 0;
}
