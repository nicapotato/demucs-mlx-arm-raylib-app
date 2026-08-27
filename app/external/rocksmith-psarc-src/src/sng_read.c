#include "sng_read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const uint8_t *d;
  size_t n;
  size_t i;
} Br;

static int br_err(Br *b, const char *msg, char **errmsg) {
  if (errmsg) {
    char buf[160];
    if (b) {
      snprintf(buf, sizeof buf, "%s (offset %zu, len %zu)", msg, b->i, b->n);
    } else {
      snprintf(buf, sizeof buf, "%s", msg);
    }
    *errmsg = strdup(buf);
  }
  return -1;
}

static int br_need(Br *b, size_t k, char **errmsg) {
  if (b->i + k > b->n) {
    return br_err(b, "SNG truncated", errmsg);
  }
  return 0;
}

static uint32_t br_u32(Br *b, char **err) {
  if (br_need(b, 4, err)) {
    return 0;
  }
  const uint8_t *p = b->d + b->i;
  uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  b->i += 4;
  return v;
}

static int32_t br_i32(Br *b, char **err) { return (int32_t)br_u32(b, err); }

static uint16_t br_u16(Br *b, char **err) {
  if (br_need(b, 2, err)) {
    return 0;
  }
  const uint8_t *p = b->d + b->i;
  uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
  b->i += 2;
  return v;
}

static int16_t br_i16(Br *b, char **err) { return (int16_t)br_u16(b, err); }

static int8_t br_i8(Br *b, char **err) {
  if (br_need(b, 1, err)) {
    return 0;
  }
  return (int8_t)b->d[b->i++];
}

static float br_f32(Br *b, char **err) {
  union {
    uint32_t u;
    float f;
  } u;
  u.u = br_u32(b, err);
  return u.f;
}

static int br_skip(Br *b, size_t k, char **err) {
  if (br_need(b, k, err)) {
    return -1;
  }
  b->i += k;
  return 0;
}

typedef struct {
  float *t;
  float *s;
  size_t len;
  size_t cap;
} BendPool;

static int bend_pool_append(BendPool *p, float time, float step) {
  if (p->len + 1 > p->cap) {
    size_t nc = p->cap ? p->cap * 2 : 16u;
    float *nt = (float *)realloc(p->t, nc * sizeof(float));
    float *ns = (float *)realloc(p->s, nc * sizeof(float));
    if (!nt || !ns) {
      free(nt);
      free(ns);
      return -1;
    }
    p->t = nt;
    p->s = ns;
    p->cap = nc;
  }
  p->t[p->len] = time;
  p->s[p->len] = step;
  p->len++;
  return 0;
}

static void bend_pool_free(BendPool *p) {
  free(p->t);
  free(p->s);
  memset(p, 0, sizeof(*p));
}

static int read_bend_data_section_into_pool(Br *b, BendPool *pool, int *out_count, int *out_offset, char **err) {
  int32_t cnt = (int32_t)br_u32(b, err);
  if (cnt < 0 || cnt > 1000000) {
    return br_err(b, "Invalid bend data count", err);
  }
  *out_offset = (int)pool->len;
  *out_count = (int)cnt;
  for (int32_t i = 0; i < cnt; i++) {
    float t = br_f32(b, err);
    float s = br_f32(b, err);
    if (br_skip(b, 4, err)) {
      return -1;
    }
    if (bend_pool_append(pool, t, s)) {
      return br_err(b, "OOM", err);
    }
  }
  return 0;
}

static int read_bend_data_big_skip(Br *b, char **err) {
  for (int k = 0; k < 32; k++) {
    if (br_skip(b, 12, err)) {
      return -1;
    }
  }
  (void)br_i32(b, err);
  return 0;
}

static int read_chord_notes_row(Br *b, RsChordNotesRow *row, char **err) {
  for (int s = 0; s < 6; s++) {
    row->str_mask[s] = br_u32(b, err);
  }
  for (int s = 0; s < 6; s++) {
    if (read_bend_data_big_skip(b, err)) {
      return -1;
    }
  }
  for (int s = 0; s < 6; s++) {
    row->slide_to[s] = br_i8(b, err);
  }
  for (int s = 0; s < 6; s++) {
    row->slide_unpitch[s] = br_i8(b, err);
  }
  for (int s = 0; s < 6; s++) {
    row->vibrato[s] = br_i16(b, err);
  }
  return 0;
}

static int read_chords_section(Br *b, RsChordTemplate **out, size_t *outc, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0 || c > 100000) {
    return br_err(b, "Bad chord count", err);
  }
  *outc = (size_t)c;
  if (c == 0) {
    *out = NULL;
    return 0;
  }
  RsChordTemplate *arr = (RsChordTemplate *)calloc((size_t)c, sizeof(RsChordTemplate));
  if (!arr) {
    return br_err(b, "OOM", err);
  }
  for (int32_t i = 0; i < c; i++) {
    arr[i].mask = br_u32(b, err);
    for (int s = 0; s < 6; s++) {
      arr[i].frets[s] = br_i8(b, err);
    }
    for (int s = 0; s < 6; s++) {
      arr[i].fingers[s] = br_i8(b, err);
    }
    for (int s = 0; s < 6; s++) {
      arr[i].notes[s] = br_i32(b, err);
    }
    if (br_need(b, RS_SNG_NAME_LEN, err)) {
      free(arr);
      return -1;
    }
    memcpy(arr[i].name, b->d + b->i, RS_SNG_NAME_LEN);
    b->i += RS_SNG_NAME_LEN;
    char *z = memchr(arr[i].name, '\0', RS_SNG_NAME_LEN);
    if (!z) {
      arr[i].name[RS_SNG_NAME_LEN - 1] = '\0';
    }
  }
  *out = arr;
  return 0;
}

static int read_chord_notes_array(Br *b, RsChordNotesRow **out, size_t *outc, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0 || c > 100000) {
    return br_err(b, "Bad chord notes count", err);
  }
  *outc = (size_t)c;
  if (c == 0) {
    *out = NULL;
    return 0;
  }
  RsChordNotesRow *arr = (RsChordNotesRow *)calloc((size_t)c, sizeof(RsChordNotesRow));
  if (!arr) {
    return br_err(b, "OOM", err);
  }
  for (int i = 0; i < c; i++) {
    if (read_chord_notes_row(b, &arr[i], err)) {
      free(arr);
      return -1;
    }
  }
  *out = arr;
  return 0;
}

static void free_bpm_section(RsSngParsed *o) {
  free(o->bpm_times);
  free(o->bpm_measures);
  free(o->bpm_beats);
  o->bpm_times = NULL;
  o->bpm_measures = NULL;
  o->bpm_beats = NULL;
  o->bpm_count = 0;
}

static int read_bpm_section(Br *b, RsSngParsed *o, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0 || c > 1000000) {
    return br_err(b, "Bad BPM count", err);
  }
  o->bpm_count = c;
  if (c == 0) {
    return 0;
  }
  o->bpm_times = (float *)calloc((size_t)c, sizeof(float));
  o->bpm_measures = (int16_t *)calloc((size_t)c, sizeof(int16_t));
  o->bpm_beats = (int16_t *)calloc((size_t)c, sizeof(int16_t));
  if (!o->bpm_times || !o->bpm_measures || !o->bpm_beats) {
    free_bpm_section(o);
    return br_err(b, "OOM", err);
  }
  for (int32_t i = 0; i < c; i++) {
    o->bpm_times[i] = br_f32(b, err);
    o->bpm_measures[i] = (int16_t)br_u16(b, err);
    o->bpm_beats[i] = (int16_t)br_u16(b, err);
    (void)br_i32(b, err);
    (void)br_i32(b, err);
  }
  return 0;
}

static int skip_phrases(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad phrase count", err);
  }
  return br_skip(b, (size_t)c * 44u, err);
}

static int read_vocals_section(Br *b, RsSngParsed *o, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad vocal count", err);
  }
  o->vocal_count = c;
  if (c == 0) {
    return 0;
  }
  o->vocal_times = (float *)calloc((size_t)c, sizeof(float));
  o->vocal_lengths = (float *)calloc((size_t)c, sizeof(float));
  o->vocal_notes = (int32_t *)calloc((size_t)c, sizeof(int32_t));
  o->vocal_lyrics = (char **)calloc((size_t)c, sizeof(char *));
  if (!o->vocal_times || !o->vocal_lengths || !o->vocal_notes || !o->vocal_lyrics) {
    return br_err(b, "OOM", err);
  }
  for (int i = 0; i < c; i++) {
    float t = br_f32(b, err);
    int32_t note = br_i32(b, err);
    float len = br_f32(b, err);
    if (br_need(b, 48, err)) {
      return -1;
    }
    char lyric[49];
    memcpy(lyric, b->d + b->i, 48);
    lyric[48] = '\0';
    b->i += 48;
    o->vocal_times[i] = t;
    o->vocal_notes[i] = note;
    o->vocal_lengths[i] = len;
    o->vocal_lyrics[i] = strdup(lyric);
    if (!o->vocal_lyrics[i]) {
      return br_err(b, "OOM", err);
    }
  }
  return 0;
}

static int skip_symbols_header(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad symbols header count", err);
  }
  return br_skip(b, (size_t)c * 32u, err);
}

static int skip_symbols_texture(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad symbols texture count", err);
  }
  return br_skip(b, (size_t)c * 144u, err);
}

static int skip_symbol_definitions(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad symbol def count", err);
  }
  return br_skip(b, (size_t)c * 44u, err);
}

static int skip_phrase_iterations(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad phrase iter count", err);
  }
  return br_skip(b, (size_t)c * 24u, err);
}

static int skip_phrase_extra(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad phrase extra count", err);
  }
  return br_skip(b, (size_t)c * 16u, err);
}

static int skip_nld(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad NLD count", err);
  }
  for (int i = 0; i < c; i++) {
    (void)br_i32(b, err);
    int32_t pc = (int32_t)br_u32(b, err);
    if (pc < 0 || pc > 100000) {
      return br_err(b, "Bad NLD phrase count", err);
    }
    if (br_skip(b, (size_t)pc * 4u, err)) {
      return -1;
    }
  }
  return 0;
}

static int skip_actions(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad action count", err);
  }
  return br_skip(b, (size_t)c * 260u, err);
}

static int skip_events(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad event count", err);
  }
  return br_skip(b, (size_t)c * 260u, err);
}

static int skip_tones(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad tone count", err);
  }
  return br_skip(b, (size_t)c * 8u, err);
}

static int skip_dnas(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad DNA count", err);
  }
  return br_skip(b, (size_t)c * 8u, err);
}

static int cmp_rs_section(const void *a, const void *b) {
  const RsSection *x = (const RsSection *)a;
  const RsSection *y = (const RsSection *)b;
  if (x->start_time_sec < y->start_time_sec) {
    return -1;
  }
  if (x->start_time_sec > y->start_time_sec) {
    return 1;
  }
  return 0;
}

static int read_zero_term_name(Br *b, char *out, size_t out_sz, char **err) {
  if (br_need(b, 32, err)) {
    return -1;
  }
  memcpy(out, b->d + b->i, 32);
  out[out_sz - 1] = '\0';
  b->i += 32;
  return 0;
}

static int read_sections_section(Br *b, RsSngParsed *o, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0 || c > 512) {
    return br_err(b, "Bad section count", err);
  }
  if (c == 0) {
    return 0;
  }
  o->sections = (RsSection *)calloc((size_t)c, sizeof(RsSection));
  if (!o->sections) {
    return br_err(b, "Section alloc failed", err);
  }
  o->section_count = (size_t)c;
  for (int32_t i = 0; i < c; i++) {
    RsSection *s = &o->sections[i];
    if (read_zero_term_name(b, s->name, sizeof s->name, err)) {
      return -1;
    }
    s->number = br_i32(b, err);
    s->start_time_sec = br_f32(b, err);
    if (br_skip(b, 48, err)) {
      return -1;
    }
  }
  qsort(o->sections, o->section_count, sizeof(RsSection), cmp_rs_section);
  return 0;
}

static int read_note_rich(Br *b, float *time, int *str, int *fret, int8_t *anchor_fret, int8_t *anchor_width, float *sus,
                          uint32_t *mask, int32_t *chord_id, int32_t *chord_notes_id, int8_t *pickdir, float *max_b,
                          BendPool *pool, int *bend_n, int *bend_off, char **err) {
  *mask = br_u32(b, err);
  (void)br_u32(b, err);
  (void)br_u32(b, err);
  *time = br_f32(b, err);
  *str = (int)br_i8(b, err);
  *fret = (int)br_i8(b, err);
  *anchor_fret = br_i8(b, err);
  *anchor_width = br_i8(b, err);
  *chord_id = br_i32(b, err);
  *chord_notes_id = br_i32(b, err);
  (void)br_i32(b, err);
  (void)br_i32(b, err);
  (void)br_i16(b, err);
  (void)br_i16(b, err);
  (void)br_i16(b, err);
  (void)br_i16(b, err);
  (void)br_i16(b, err);
  (void)br_i8(b, err);
  (void)br_i8(b, err);
  (void)br_i8(b, err);
  (void)br_i8(b, err);
  *pickdir = br_i8(b, err);
  (void)br_i8(b, err);
  (void)br_i8(b, err);
  (void)br_i16(b, err);
  *sus = br_f32(b, err);
  *max_b = br_f32(b, err);
  return read_bend_data_section_into_pool(b, pool, bend_n, bend_off, err);
}

static void free_ad_anchors(RsArrangementData *ad) {
  free(ad->anchors);
  ad->anchors = NULL;
  ad->anchor_count = 0;
}

static void free_ad_notes(RsArrangementData *ad) {
  free_ad_anchors(ad);
  free(ad->note_times);
  free(ad->note_strings);
  free(ad->note_frets);
  free(ad->note_anchor_frets);
  free(ad->note_anchor_widths);
  free(ad->note_sustains);
  free(ad->note_masks);
  free(ad->chord_ids);
  free(ad->chord_notes_ids);
  free(ad->pick_directions);
  free(ad->max_bends);
  free(ad->bend_value_counts);
  free(ad->bend_pair_offsets);
  free(ad->bend_time_sec);
  free(ad->bend_step);
  memset(ad, 0, sizeof(*ad));
}

static int read_notes_section(Br *b, RsArrangementData *ad, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0 || c > 500000) {
    return br_err(b, "Bad note count", err);
  }
  ad->note_count = c;
  if (c == 0) {
    return 0;
  }
  ad->note_times = (float *)calloc((size_t)c, sizeof(float));
  ad->note_strings = (int32_t *)calloc((size_t)c, sizeof(int32_t));
  ad->note_frets = (int32_t *)calloc((size_t)c, sizeof(int32_t));
  ad->note_anchor_frets = (int8_t *)calloc((size_t)c, sizeof(int8_t));
  ad->note_anchor_widths = (int8_t *)calloc((size_t)c, sizeof(int8_t));
  ad->note_sustains = (float *)calloc((size_t)c, sizeof(float));
  ad->note_masks = (uint32_t *)calloc((size_t)c, sizeof(uint32_t));
  ad->chord_ids = (int32_t *)calloc((size_t)c, sizeof(int32_t));
  ad->chord_notes_ids = (int32_t *)calloc((size_t)c, sizeof(int32_t));
  ad->pick_directions = (int8_t *)calloc((size_t)c, sizeof(int8_t));
  ad->max_bends = (float *)calloc((size_t)c, sizeof(float));
  ad->bend_value_counts = (int *)calloc((size_t)c, sizeof(int));
  ad->bend_pair_offsets = (int *)calloc((size_t)((size_t)c + 1u), sizeof(int));
  if (!ad->note_times || !ad->note_strings || !ad->note_frets || !ad->note_anchor_frets || !ad->note_anchor_widths ||
      !ad->note_sustains || !ad->note_masks ||
      !ad->chord_ids || !ad->chord_notes_ids || !ad->pick_directions || !ad->max_bends || !ad->bend_value_counts ||
      !ad->bend_pair_offsets) {
    free_ad_notes(ad);
    return br_err(b, "OOM", err);
  }
  BendPool pool = {0};
  for (int32_t i = 0; i < c; i++) {
    int bcnt = 0, boff = 0;
    if (read_note_rich(b, &ad->note_times[i], (int *)&ad->note_strings[i], (int *)&ad->note_frets[i],
                       &ad->note_anchor_frets[i], &ad->note_anchor_widths[i], &ad->note_sustains[i], &ad->note_masks[i],
                       &ad->chord_ids[i], &ad->chord_notes_ids[i], &ad->pick_directions[i], &ad->max_bends[i], &pool, &bcnt,
                       &boff, err)) {
      bend_pool_free(&pool);
      free_ad_notes(ad);
      return -1;
    }
    ad->bend_value_counts[i] = bcnt;
    ad->bend_pair_offsets[i] = boff;
  }
  ad->bend_pair_offsets[c] = (int)pool.len;
  if (pool.len > 0) {
    ad->bend_time_sec = (float *)malloc(pool.len * sizeof(float));
    ad->bend_step = (float *)malloc(pool.len * sizeof(float));
    if (!ad->bend_time_sec || !ad->bend_step) {
      free(ad->bend_time_sec);
      free(ad->bend_step);
      bend_pool_free(&pool);
      free_ad_notes(ad);
      return br_err(b, "OOM", err);
    }
    memcpy(ad->bend_time_sec, pool.t, pool.len * sizeof(float));
    memcpy(ad->bend_step, pool.s, pool.len * sizeof(float));
  }
  bend_pool_free(&pool);
  return 0;
}

static int read_anchors_section(Br *b, RsArrangementData *ad, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0 || c > 100000) {
    return br_err(b, "Bad anchor count", err);
  }
  ad->anchor_count = c;
  if (c == 0) {
    ad->anchors = NULL;
    return 0;
  }
  ad->anchors = (RsSngAnchor *)calloc((size_t)c, sizeof(RsSngAnchor));
  if (!ad->anchors) {
    return br_err(b, "OOM", err);
  }
  for (int32_t i = 0; i < c; i++) {
    float start_time = br_f32(b, err);
    (void)br_f32(b, err);
    (void)br_f32(b, err);
    (void)br_f32(b, err);
    int fret = (int)br_i8(b, err);
    if (br_skip(b, 3, err)) {
      return -1;
    }
    int width = (int)br_i32(b, err);
    (void)br_i32(b, err);
    if (fret < 1) {
      fret = 1;
    }
    if (width < 1) {
      width = 4;
    }
    ad->anchors[i].time_sec = start_time;
    ad->anchors[i].fret = fret;
    ad->anchors[i].width = width;
  }
  return 0;
}

static int skip_anchor_ext(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad anchor ext count", err);
  }
  return br_skip(b, (size_t)c * 12u, err);
}

static int skip_fingerprints(Br *b, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0) {
    return br_err(b, "Bad fingerprint count", err);
  }
  return br_skip(b, (size_t)c * 20u, err);
}

static int read_arrangement(Br *b, RsArrangementData *ad, char **err) {
  ad->difficulty = br_i32(b, err);
  if (read_anchors_section(b, ad, err)) {
    return -1;
  }
  if (skip_anchor_ext(b, err)) {
    return -1;
  }
  if (skip_fingerprints(b, err)) {
    return -1;
  }
  if (skip_fingerprints(b, err)) {
    return -1;
  }
  if (read_notes_section(b, ad, err)) {
    return -1;
  }
  int32_t phrase_count = (int32_t)br_u32(b, err);
  if (phrase_count < 0 || phrase_count > 100000) {
    return br_err(b, "Bad phrase_count", err);
  }
  if (br_skip(b, (size_t)phrase_count * 4u, err)) {
    return -1;
  }
  int32_t pic1 = (int32_t)br_u32(b, err);
  if (pic1 < 0 || pic1 > 1000000) {
    return br_err(b, "Bad phrase iter 1", err);
  }
  if (br_skip(b, (size_t)pic1 * 4u, err)) {
    return -1;
  }
  int32_t pic2 = (int32_t)br_u32(b, err);
  if (pic2 < 0 || pic2 > 1000000) {
    return br_err(b, "Bad phrase iter 2", err);
  }
  if (br_skip(b, (size_t)pic2 * 4u, err)) {
    return -1;
  }
  return 0;
}

static int read_arrangement_section(Br *b, RsSngParsed *out, char **err) {
  int32_t c = (int32_t)br_u32(b, err);
  if (c < 0 || c > 256) {
    return br_err(b, "Bad arrangement count", err);
  }
  if (c == 0) {
    out->arrangements = NULL;
    out->arrangement_count = 0;
    return 0;
  }
  RsArrangementData *arr = (RsArrangementData *)calloc((size_t)c, sizeof(RsArrangementData));
  if (!arr) {
    return br_err(b, "OOM", err);
  }
  for (int i = 0; i < c; i++) {
    if (read_arrangement(b, &arr[i], err)) {
      for (int j = 0; j <= i; j++) {
        free_ad_notes(&arr[j]);
      }
      free(arr);
      return -1;
    }
  }
  out->arrangements = arr;
  out->arrangement_count = (size_t)c;
  return 0;
}

static int read_metadata(Br *b, RsSngParsed *out, char **err) {
  if (br_skip(b, 32, err)) {
    return -1;
  }
  (void)br_f32(b, err);
  (void)br_f32(b, err);
  (void)br_i8(b, err);
  if (br_skip(b, 32, err)) {
    return -1;
  }
  (void)br_i16(b, err);
  (void)br_f32(b, err);
  int32_t string_count = (int32_t)br_u32(b, err);
  if (string_count < 0 || string_count > 32) {
    return br_err(b, "Bad string count in metadata", err);
  }
  if (br_skip(b, (size_t)string_count * 2u, err)) {
    return -1;
  }
  (void)br_f32(b, err);
  (void)br_f32(b, err);
  out->metadata_max_difficulty = br_i32(b, err);
  return 0;
}

void rs_sng_parsed_free(RsSngParsed *p) {
  if (!p) {
    return;
  }
  free_bpm_section(p);
  if (p->arrangements) {
    for (size_t i = 0; i < p->arrangement_count; i++) {
      free_ad_notes(&p->arrangements[i]);
    }
    free(p->arrangements);
  }
  free(p->chords);
  free(p->chord_note_rows);
  if (p->vocal_lyrics) {
    for (int i = 0; i < p->vocal_count; i++) {
      free(p->vocal_lyrics[i]);
    }
  }
  free(p->vocal_times);
  free(p->vocal_lengths);
  free(p->vocal_notes);
  free(p->vocal_lyrics);
  free(p->sections);
  memset(p, 0, sizeof(*p));
}

int rs_sng_parse_sections_blob(const uint8_t *data, size_t len, RsSection **out, size_t *out_count, char **errmsg) {
  if (!out || !out_count) {
    return br_err(NULL, "Null out pointer", errmsg);
  }
  *out = NULL;
  *out_count = 0;
  RsSngParsed tmp = {0};
  Br b = {.d = data, .n = len, .i = 0};
  if (read_sections_section(&b, &tmp, errmsg)) {
    return -1;
  }
  *out = tmp.sections;
  *out_count = tmp.section_count;
  tmp.sections = NULL;
  return 0;
}

int rs_sng_parse_blob(const uint8_t *data, size_t len, RsSngParsed *out, int want_vocal_text, char **errmsg) {
  (void)want_vocal_text;
  memset(out, 0, sizeof(*out));
  Br b = {.d = data, .n = len, .i = 0};
  Br *br = &b;

  if (read_bpm_section(br, out, errmsg)) {
    return -1;
  }
  if (skip_phrases(br, errmsg)) {
    return -1;
  }
  if (read_chords_section(br, &out->chords, &out->chord_count, errmsg)) {
    return -1;
  }
  if (read_chord_notes_array(br, &out->chord_note_rows, &out->chord_note_count, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (read_vocals_section(br, out, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (out->vocal_count > 0) {
    if (skip_symbols_header(br, errmsg)) {
      rs_sng_parsed_free(out);
      return -1;
    }
    if (skip_symbols_texture(br, errmsg)) {
      rs_sng_parsed_free(out);
      return -1;
    }
    if (skip_symbol_definitions(br, errmsg)) {
      rs_sng_parsed_free(out);
      return -1;
    }
  }
  if (skip_phrase_iterations(br, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (skip_phrase_extra(br, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (skip_nld(br, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (skip_actions(br, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (skip_events(br, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (skip_tones(br, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (skip_dnas(br, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (read_sections_section(br, out, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (read_arrangement_section(br, out, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (read_metadata(br, out, errmsg)) {
    rs_sng_parsed_free(out);
    return -1;
  }
  if (br->i > br->n) {
    rs_sng_parsed_free(out);
    return br_err(br, "SNG over-read", errmsg);
  }
  return 0;
}
