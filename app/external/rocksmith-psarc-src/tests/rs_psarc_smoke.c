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

static int contains(const char *hay, const char *needle) {
  return strstr(hay, needle) != NULL;
}

static const char *pick_path(RsPsarc *p, int (*pred)(const char *)) {
  size_t n = rs_psarc_file_count(p);
  const char *best = NULL;
  for (size_t i = 0; i < n; i++) {
    const char *nm = rs_psarc_file_name(p, i);
    if (!nm || !nm[0]) {
      continue;
    }
    if (pred(nm)) {
      if (!best || strcmp(nm, best) < 0) {
        best = nm;
      }
    }
  }
  return best;
}

static int is_lead_sng(const char *nm) {
  if (!ends_with(nm, ".sng")) {
    return 0;
  }
  if (!contains(nm, "lead")) {
    return 0;
  }
  return contains(nm, "songs/bin") || contains(nm, "songs\\bin");
}

static int is_bass_sng(const char *nm) {
  if (!ends_with(nm, ".sng")) {
    return 0;
  }
  if (!contains(nm, "bass")) {
    return 0;
  }
  return contains(nm, "songs/bin") || contains(nm, "songs\\bin");
}

static int is_vocal_sng(const char *nm) {
  if (!ends_with(nm, ".sng")) {
    return 0;
  }
  if (!contains(nm, "vocals")) {
    return 0;
  }
  return contains(nm, "songs/bin") || contains(nm, "songs\\bin");
}

static int check_psarc(const char *path) {
  char *err = NULL;
  RsPsarc *p = rs_psarc_open(path, &err);
  if (!p) {
    fprintf(stderr, "open %s: %s\n", path, err ? err : "?");
    free(err);
    return 1;
  }

  const char *lead = pick_path(p, is_lead_sng);
  const char *bass = pick_path(p, is_bass_sng);
  const char *voc = pick_path(p, is_vocal_sng);

  printf("PSARC: %s\n", path);
  printf("  lead:  %s\n", lead ? lead : "(missing)");
  printf("  bass:  %s\n", bass ? bass : "(missing)");
  printf("  vocal: %s\n", voc ? voc : "(missing)");

  int rc = 0;
  uint8_t *buf = NULL;
  size_t len = 0;

  if (!lead || rs_psarc_read_file(p, lead, &buf, &len, &err)) {
    fprintf(stderr, "  lead read failed: %s\n", err ? err : "?");
    free(err);
    err = NULL;
    rc = 1;
  } else {
    RsChart ch = {0};
    if (rs_chart_parse_instrument_sng(buf, len, RS_SNG_PLATFORM_PC, &ch, &err)) {
      fprintf(stderr, "  lead parse: %s\n", err ? err : "?");
      free(err);
      err = NULL;
      rc = 1;
    } else {
      printf("  lead notes: %zu\n", ch.note_count);
      if (ch.note_count == 0) {
        rc = 1;
      }
    }
    rs_chart_free(&ch);
    free(buf);
    buf = NULL;
  }

  if (!bass || rs_psarc_read_file(p, bass, &buf, &len, &err)) {
    fprintf(stderr, "  bass read failed: %s\n", err ? err : "?");
    free(err);
    err = NULL;
    rc = 1;
  } else {
    RsChart ch = {0};
    if (rs_chart_parse_instrument_sng(buf, len, RS_SNG_PLATFORM_PC, &ch, &err)) {
      fprintf(stderr, "  bass parse: %s\n", err ? err : "?");
      free(err);
      err = NULL;
      rc = 1;
    } else {
      printf("  bass notes: %zu\n", ch.note_count);
      if (ch.note_count == 0) {
        rc = 1;
      }
    }
    rs_chart_free(&ch);
    free(buf);
    buf = NULL;
  }

  if (!voc || rs_psarc_read_file(p, voc, &buf, &len, &err)) {
    fprintf(stderr, "  vocal read failed: %s\n", err ? err : "?");
    free(err);
    err = NULL;
    rc = 1;
  } else {
    RsChart ch = {0};
    if (rs_chart_parse_vocal_sng(buf, len, RS_SNG_PLATFORM_PC, &ch, &err)) {
      fprintf(stderr,  "  vocal parse: %s\n", err ? err : "?");
      free(err);
      err = NULL;
      rc = 1;
    } else {
      printf("  vocal events: %zu\n", ch.vocal_count);
      if (ch.vocal_count == 0) {
        rc = 1;
      }
    }
    rs_chart_free(&ch);
    free(buf);
  }

  rs_psarc_close(p);
  return rc;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: rs_psarc_smoke <a.psarc> <b.psarc>\n");
    return 2;
  }
  int r1 = check_psarc(argv[1]);
  int r2 = check_psarc(argv[2]);
  return (r1 || r2) ? 1 : 0;
}
