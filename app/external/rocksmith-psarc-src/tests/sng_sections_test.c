#include "rocksmith_psarc.h"
#include "sng_read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void put_i32(uint8_t *p, int32_t v) { put_u32(p, (uint32_t)v); }

static void put_f32(uint8_t *p, float f) {
  union {
    float f;
    uint32_t u;
  } u;
  u.f = f;
  put_u32(p, u.u);
}

static void write_section(uint8_t *p, const char *name, int32_t number, float start_time) {
  memset(p, 0, 88);
  strncpy((char *)p, name, 31);
  put_i32(p + 32, number);
  put_f32(p + 36, start_time);
}

static int test_two_sections(void) {
  uint8_t buf[4 + 88 * 2];
  put_u32(buf, 2);
  write_section(buf + 4, "chorus", 2, 78.077f);
  write_section(buf + 4 + 88, "intro", 1, 11.154f);

  RsSection *sections = NULL;
  size_t count = 0;
  char *err = NULL;
  if (rs_sng_parse_sections_blob(buf, sizeof buf, &sections, &count, &err) != 0) {
    fprintf(stderr, "parse failed: %s\n", err ? err : "?");
    free(err);
    return 1;
  }
  if (count != 2) {
    fprintf(stderr, "expected 2 sections, got %zu\n", count);
    free(sections);
    return 1;
  }
  if (strcmp(sections[0].name, "intro") != 0 || sections[0].number != 1 || sections[0].start_time_sec < 11.153f ||
      sections[0].start_time_sec > 11.155f) {
    fprintf(stderr, "first section mismatch (sorted by time)\n");
    free(sections);
    return 1;
  }
  if (strcmp(sections[1].name, "chorus") != 0 || sections[1].number != 2) {
    fprintf(stderr, "second section mismatch\n");
    free(sections);
    return 1;
  }
  free(sections);
  return 0;
}

int main(void) {
  if (test_two_sections() != 0) {
    return 1;
  }
  printf("sng_sections_test: ok\n");
  return 0;
}
