#include "rocksmith_psarc.h"
#include "rs_sng_mask.h"
#include "sng_read.h"
#include "sng_unpack.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void rs_chart_init(RsChart *c) { memset(c, 0, sizeof(*c)); }

void rs_chart_free(RsChart *c) {
  if (!c) {
    return;
  }
  free(c->notes);
  free(c->anchors);
  free(c->vocals);
  free(c->chord_templates);
  free(c->sections);
  rs_chart_init(c);
}

static void copy_sections_from_parsed(const RsSngParsed *parsed, RsChart *out) {
  if (parsed->section_count == 0 || !parsed->sections) {
    return;
  }
  out->sections = (RsSection *)calloc(parsed->section_count, sizeof(RsSection));
  if (!out->sections) {
    return;
  }
  out->section_count = parsed->section_count;
  memcpy(out->sections, parsed->sections, parsed->section_count * sizeof(RsSection));
}

static void copy_chord_templates_from_parsed(const RsSngParsed *parsed, RsChart *out) {
  if (parsed->chord_count == 0 || !parsed->chords) {
    return;
  }
  out->chord_templates = (RsChordTemplateEntry *)calloc(parsed->chord_count, sizeof(RsChordTemplateEntry));
  if (!out->chord_templates) {
    return;
  }
  out->chord_template_count = parsed->chord_count;
  for (size_t ci = 0; ci < parsed->chord_count; ci++) {
    const RsChordTemplate *src = &parsed->chords[ci];
    RsChordTemplateEntry *dst = &out->chord_templates[ci];
    dst->id = (int32_t)ci;
    memcpy(dst->frets, src->frets, sizeof dst->frets);
    memcpy(dst->fingers, src->fingers, sizeof dst->fingers);
    strncpy(dst->name, src->name, sizeof dst->name - 1);
    dst->name[sizeof dst->name - 1] = '\0';
  }
}

#define RS_ANCHOR_TOP_DIFFICULTY_TIERS 2
#define RS_ANCHOR_TIME_EPS 1.0e-4f

typedef struct {
  float time_sec;
  int fret;
  int width;
  int32_t difficulty;
} AnchorScratch;

static int cmp_anchor_scratch(const void *a, const void *b) {
  const AnchorScratch *x = (const AnchorScratch *)a;
  const AnchorScratch *y = (const AnchorScratch *)b;
  if (x->time_sec < y->time_sec) {
    return -1;
  }
  if (x->time_sec > y->time_sec) {
    return 1;
  }
  if (x->difficulty < y->difficulty) {
    return -1;
  }
  if (x->difficulty > y->difficulty) {
    return 1;
  }
  return 0;
}

static int note_row_anchor_valid(int8_t af, int8_t aw) {
  if (af < 1) {
    return 0;
  }
  if (aw < 1) {
    return aw >= 0 && af >= 1;
  }
  return 1;
}

static void append_anchor_scratch(AnchorScratch **scratch, size_t *scratch_n, size_t *scratch_cap, float time_sec, int fret,
                                  int width, int32_t difficulty) {
  if (*scratch_n + 1 > *scratch_cap) {
    size_t nc = *scratch_cap ? *scratch_cap * 2 : 32;
    AnchorScratch *nb = (AnchorScratch *)realloc(*scratch, nc * sizeof(AnchorScratch));
    if (!nb) {
      return;
    }
    *scratch = nb;
    *scratch_cap = nc;
  }
  (*scratch)[*scratch_n].time_sec = time_sec;
  (*scratch)[*scratch_n].fret = fret;
  (*scratch)[*scratch_n].width = width < 1 ? 4 : width;
  (*scratch)[*scratch_n].difficulty = difficulty;
  (*scratch_n)++;
}

static void finalize_anchor_scratch(AnchorScratch *scratch, size_t scratch_n, RsChart *out) {
  if (scratch_n == 0) {
    return;
  }
  qsort(scratch, scratch_n, sizeof(AnchorScratch), cmp_anchor_scratch);
  size_t out_n = 0;
  for (size_t i = 0; i < scratch_n; i++) {
    if (out_n > 0 && fabsf(scratch[i].time_sec - scratch[out_n - 1].time_sec) < RS_ANCHOR_TIME_EPS) {
      scratch[out_n - 1] = scratch[i];
      continue;
    }
    scratch[out_n++] = scratch[i];
  }
  out->anchors = (RsAnchor *)calloc(out_n, sizeof(RsAnchor));
  if (!out->anchors) {
    return;
  }
  out->anchor_count = out_n;
  for (size_t i = 0; i < out_n; i++) {
    out->anchors[i].time_sec = scratch[i].time_sec;
    out->anchors[i].fret = scratch[i].fret;
    out->anchors[i].width = scratch[i].width;
  }
}

/* Merge anchors from the top N difficulty tiers (e.g. max and max-1). */
static void copy_anchors_from_top_difficulties(const RsSngParsed *parsed, RsChart *out, int top_tiers) {
  if (top_tiers < 1) {
    top_tiers = 1;
  }
  int32_t cap_diff = parsed->metadata_max_difficulty;
  if (cap_diff < 0) {
    cap_diff = 0;
  }
  int32_t min_diff = cap_diff - (int32_t)top_tiers + 1;
  if (min_diff < 0) {
    min_diff = 0;
  }

  size_t scratch_cap = 0;
  size_t scratch_n = 0;
  AnchorScratch *scratch = NULL;

  for (size_t a = 0; a < parsed->arrangement_count; a++) {
    const RsArrangementData *ad = &parsed->arrangements[a];
    if (ad->difficulty < min_diff || ad->difficulty > cap_diff) {
      continue;
    }
    if (ad->anchors && ad->anchor_count > 0) {
      for (int i = 0; i < ad->anchor_count; i++) {
        append_anchor_scratch(&scratch, &scratch_n, &scratch_cap, ad->anchors[i].time_sec, ad->anchors[i].fret,
                              ad->anchors[i].width, ad->difficulty);
      }
    }
  }

  if (scratch_n == 0) {
    free(scratch);
    return;
  }

  finalize_anchor_scratch(scratch, scratch_n, out);
  free(scratch);
}

/* Rocksmith stores incremental tiers: top tiers may only anchor late phrases. Pull
 * arrangement + per-note anchors from tiers below the top-N window, but only before
 * the first top-tier anchor so we do not reintroduce all-tier conflicts. */
static void prepend_early_anchors_below_top_tiers(const RsSngParsed *parsed, RsChart *out, int top_tiers) {
  if (!out->anchors || out->anchor_count == 0) {
    return;
  }
  float cutoff = out->anchors[0].time_sec;
  if (cutoff <= 0.0f) {
    return;
  }
  int32_t cap_diff = parsed->metadata_max_difficulty;
  if (cap_diff < 0) {
    cap_diff = 0;
  }
  int32_t min_top = cap_diff - (int32_t)top_tiers + 1;
  if (min_top < 0) {
    min_top = 0;
  }

  AnchorScratch *scratch = NULL;
  size_t scratch_n = 0, scratch_cap = 0;

  for (size_t a = 0; a < parsed->arrangement_count; a++) {
    const RsArrangementData *ad = &parsed->arrangements[a];
    if (ad->difficulty >= min_top) {
      continue;
    }
    if (ad->anchors && ad->anchor_count > 0) {
      for (int i = 0; i < ad->anchor_count; i++) {
        if (ad->anchors[i].time_sec >= cutoff - RS_ANCHOR_TIME_EPS) {
          continue;
        }
        append_anchor_scratch(&scratch, &scratch_n, &scratch_cap, ad->anchors[i].time_sec, ad->anchors[i].fret,
                              ad->anchors[i].width, ad->difficulty);
      }
    }
    if (!ad->note_times || !ad->note_anchor_frets) {
      continue;
    }
    for (int i = 0; i < ad->note_count; i++) {
      if (ad->note_times[i] >= cutoff - RS_ANCHOR_TIME_EPS) {
        continue;
      }
      if (!note_row_anchor_valid(ad->note_anchor_frets[i], ad->note_anchor_widths[i])) {
        continue;
      }
      append_anchor_scratch(&scratch, &scratch_n, &scratch_cap, ad->note_times[i], (int)ad->note_anchor_frets[i],
                            (int)ad->note_anchor_widths[i], ad->difficulty);
    }
  }

  if (scratch_n == 0) {
    free(scratch);
    return;
  }

  size_t merged_cap = scratch_n + out->anchor_count;
  AnchorScratch *merged = (AnchorScratch *)calloc(merged_cap, sizeof(AnchorScratch));
  if (!merged) {
    free(scratch);
    return;
  }
  size_t mn = 0;
  for (size_t i = 0; i < scratch_n; i++) {
    merged[mn++] = scratch[i];
  }
  for (size_t i = 0; i < out->anchor_count; i++) {
    merged[mn].time_sec = out->anchors[i].time_sec;
    merged[mn].fret = out->anchors[i].fret;
    merged[mn].width = out->anchors[i].width;
    merged[mn].difficulty = min_top;
    mn++;
  }
  free(scratch);
  free(out->anchors);
  out->anchors = NULL;
  out->anchor_count = 0;
  finalize_anchor_scratch(merged, mn, out);
  free(merged);
}

static int pick_arrangement_index(const RsSngParsed *p) {
  int32_t target = p->metadata_max_difficulty;
  int best = -1;
  int32_t best_diff = -1;
  for (size_t i = 0; i < p->arrangement_count; i++) {
    if (p->arrangements[i].difficulty == target) {
      return (int)i;
    }
    if (p->arrangements[i].difficulty > best_diff) {
      best_diff = p->arrangements[i].difficulty;
      best = (int)i;
    }
  }
  if (best >= 0) {
    return best;
  }
  return 0;
}

static int cmp_track(const void *a, const void *b) {
  const RsTrackNote *x = (const RsTrackNote *)a;
  const RsTrackNote *y = (const RsTrackNote *)b;
  if (x->time_sec < y->time_sec) {
    return -1;
  }
  if (x->time_sec > y->time_sec) {
    return 1;
  }
  if (x->string_index < y->string_index) {
    return -1;
  }
  if (x->string_index > y->string_index) {
    return 1;
  }
  if (x->fret < y->fret) {
    return -1;
  }
  if (x->fret > y->fret) {
    return 1;
  }
  return 0;
}

static const RsChordNotesRow *chord_notes_for_row(const RsSngParsed *p, const RsArrangementData *ad, int ni) {
  if (!p || !ad || ad->chord_notes_ids[ni] < 0 || (size_t)ad->chord_notes_ids[ni] >= p->chord_note_count) {
    return NULL;
  }
  return &p->chord_note_rows[(size_t)ad->chord_notes_ids[ni]];
}

static void apply_slide_from_cn(RsTrackNote *tn, const RsChordNotesRow *cn, int string_index) {
  if (!tn || !cn || string_index < 0 || string_index >= 6) {
    return;
  }
  tn->slide_to = -1;
  tn->slide_unpitch_to = -1;
  const uint32_t mask = tn->mask | cn->str_mask[string_index];
  if ((mask & RS_SNG_NM_SLIDE) != 0 && cn->slide_to[string_index] >= 0) {
    tn->slide_to = cn->slide_to[string_index];
    return;
  }
  if ((mask & RS_SNG_NM_UNPITCHED_SLIDE) != 0 && cn->slide_unpitch[string_index] >= 0) {
    tn->slide_unpitch_to = cn->slide_unpitch[string_index];
  }
}

static void copy_bends_to_track(RsTrackNote *tn, const RsArrangementData *ad, int ni) {
  int b0 = ad->bend_pair_offsets[ni];
  int b1 = ad->bend_pair_offsets[ni + 1];
  int n = b1 - b0;
  if (n > RS_TRACK_BEND_CAP) {
    n = RS_TRACK_BEND_CAP;
  }
  tn->bend_value_count = n;
  for (int k = 0; k < n; k++) {
    tn->bend_time_sec[k] = ad->bend_time_sec[b0 + k];
    tn->bend_step[k] = ad->bend_step[b0 + k];
  }
}

/* Expand one SNG row into 0+ RsTrackNote (chords -> multiple strings; singles -> 1). */
static size_t push_note_expanded(const RsSngParsed *p, const RsArrangementData *ad, int ni, RsTrackNote **buf,
                                 size_t *len, size_t *cap, int *group_seq, char **err) {
  int32_t cid = ad->chord_ids[ni];
  float t = ad->note_times[ni];
  float sus = ad->note_sustains[ni];
  uint32_t base_m = ad->note_masks[ni];
  int8_t pdir = ad->pick_directions[ni];
  float mxb = ad->max_bends[ni];
  int8_t naf = ad->note_anchor_frets ? ad->note_anchor_frets[ni] : (int8_t)-1;
  int8_t naw = ad->note_anchor_widths ? ad->note_anchor_widths[ni] : (int8_t)-1;

  const RsChordNotesRow *cn_row = chord_notes_for_row(p, ad, ni);

  if (cid < 0 || p->chords == NULL || (size_t)cid >= p->chord_count) {
    if (*len + 1u > *cap) {
      size_t nc = *cap ? *cap * 2 : 256u;
      RsTrackNote *nb = (RsTrackNote *)realloc(*buf, nc * sizeof(RsTrackNote));
      if (!nb) {
        if (err) {
          *err = strdup("OOM");
        }
        return 0;
      }
      *buf = nb;
      *cap = nc;
    }
    RsTrackNote *tn = &(*buf)[*len];
    memset(tn, 0, sizeof(*tn));
    tn->time_sec = t;
    tn->string_index = (int)ad->note_strings[ni];
    tn->fret = (int)ad->note_frets[ni];
    tn->sustain_sec = sus;
    tn->mask = base_m;
    if (cn_row) {
      tn->mask |= cn_row->str_mask[tn->string_index];
    }
    tn->pick_direction = pdir;
    tn->max_bend = mxb;
    tn->chord_id = -1;
    tn->chord_group_id = -1;
    tn->anchor_fret = naf;
    tn->anchor_width = naw;
    apply_slide_from_cn(tn, cn_row, tn->string_index);
    copy_bends_to_track(tn, ad, ni);
    (*len)++;
    return 1;
  }

  const RsChordTemplate *tpl = &p->chords[(size_t)cid];
  const RsChordNotesRow *cn = cn_row;

  int any = 0;
  for (int s = 0; s < 6; s++) {
    if (tpl->frets[s] == (int8_t)-1) {
      continue;
    }
    any = 1;
  }
  if (!any) {
    if (*len + 1u > *cap) {
      size_t nc = *cap ? *cap * 2 : 256u;
      RsTrackNote *nb = (RsTrackNote *)realloc(*buf, nc * sizeof(RsTrackNote));
      if (!nb) {
        if (err) {
          *err = strdup("OOM");
        }
        return 0;
      }
      *buf = nb;
      *cap = nc;
    }
    RsTrackNote *tn = &(*buf)[*len];
    memset(tn, 0, sizeof(*tn));
    tn->time_sec = t;
    tn->string_index = (int)ad->note_strings[ni];
    tn->fret = (int)ad->note_frets[ni];
    tn->sustain_sec = sus;
    tn->mask = base_m;
    if (cn_row) {
      tn->mask |= cn_row->str_mask[tn->string_index];
    }
    tn->pick_direction = pdir;
    tn->max_bend = mxb;
    tn->chord_id = cid;
    tn->chord_group_id = -1;
    tn->anchor_fret = naf;
    tn->anchor_width = naw;
    apply_slide_from_cn(tn, cn, tn->string_index);
    copy_bends_to_track(tn, ad, ni);
    (*len)++;
    return 1;
  }

  int gid = *group_seq;
  (*group_seq)++;

  for (int s = 0; s < 6; s++) {
    if (tpl->frets[s] == (int8_t)-1) {
      continue;
    }
    if (*len + 1u > *cap) {
      size_t nc = *cap ? *cap * 2 : 256u;
      RsTrackNote *nb = (RsTrackNote *)realloc(*buf, nc * sizeof(RsTrackNote));
      if (!nb) {
        if (err) {
          *err = strdup("OOM");
        }
        return 0;
      }
      *buf = nb;
      *cap = nc;
    }
    RsTrackNote *tn = &(*buf)[*len];
    memset(tn, 0, sizeof(*tn));
    tn->time_sec = t;
    tn->string_index = s;
    tn->fret = (int)tpl->frets[s];
    tn->sustain_sec = sus;
    uint32_t sm = base_m;
    if (cn) {
      sm |= cn->str_mask[s];
    }
    tn->mask = sm;
    tn->pick_direction = pdir;
    tn->max_bend = mxb;
    tn->chord_id = cid;
    tn->chord_group_id = gid;
    tn->is_from_chord_expansion = 1u;
    tn->anchor_fret = naf;
    tn->anchor_width = naw;
    apply_slide_from_cn(tn, cn, s);
    copy_bends_to_track(tn, ad, ni);
    (*len)++;
  }
  return 1;
}

static void merge_slide_fields(RsTrackNote *dst, const RsTrackNote *src) {
  if ((src->mask & RS_SNG_NM_SLIDE) != 0 && src->slide_to >= 0) {
    dst->slide_to = src->slide_to;
    dst->slide_unpitch_to = -1;
    return;
  }
  if ((src->mask & RS_SNG_NM_UNPITCHED_SLIDE) != 0 && src->slide_unpitch_to >= 0 && dst->slide_to < 0) {
    dst->slide_unpitch_to = src->slide_unpitch_to;
  }
}

static RsTrackNote *dedupe_track_notes(RsTrackNote *buf, size_t nout, size_t *out_count) {
  if (!buf || nout == 0) {
    *out_count = 0;
    return buf;
  }
  qsort(buf, nout, sizeof(RsTrackNote), cmp_track);
  const float t_eps = 1.0e-4f;
  size_t w = 0;
  for (size_t i = 0; i < nout; i++) {
    if (w > 0) {
      RsTrackNote *p0 = &buf[w - 1];
      RsTrackNote *c = &buf[i];
      if (fabsf(p0->time_sec - c->time_sec) < t_eps && p0->string_index == c->string_index && p0->fret == c->fret) {
        if (c->sustain_sec > p0->sustain_sec) {
          p0->sustain_sec = c->sustain_sec;
        }
        p0->mask |= c->mask;
        if (c->max_bend > p0->max_bend) {
          p0->max_bend = c->max_bend;
        }
        for (int k = 0; k < c->bend_value_count && p0->bend_value_count < RS_TRACK_BEND_CAP; k++) {
          int o = p0->bend_value_count;
          p0->bend_time_sec[o] = c->bend_time_sec[k];
          p0->bend_step[o] = c->bend_step[k];
          p0->bend_value_count++;
        }
        merge_slide_fields(p0, c);
        continue;
      }
    }
    buf[w++] = buf[i];
  }
  if (w < nout) {
    RsTrackNote *sh = (RsTrackNote *)realloc(buf, w * sizeof(RsTrackNote));
    if (sh) {
      buf = sh;
    }
  }
  *out_count = w;
  return buf;
}

static int parse_and_expand_all(const uint8_t *data, size_t len, RsSngPlatform platform, RsChart *out, char **errmsg) {
  uint8_t *plain = NULL;
  size_t plen = 0;
  int use_mac = (platform == RS_SNG_PLATFORM_MAC);
  if (rs_sng_unpack(data, len, use_mac, &plain, &plen) != 0) {
    if (errmsg) {
      *errmsg = strdup("SNG unpack failed");
    }
    return -1;
  }
  RsSngParsed parsed = {0};
  if (rs_sng_parse_blob(plain, plen, &parsed, 0, errmsg) != 0) {
    free(plain);
    rs_sng_parsed_free(&parsed);
    return -1;
  }
  free(plain);

  if (parsed.arrangement_count == 0) {
    rs_sng_parsed_free(&parsed);
    if (errmsg) {
      *errmsg = strdup("No arrangements in SNG");
    }
    return -1;
  }

  RsTrackNote *buf = NULL;
  size_t nout = 0, cap = 0;
  int group_seq = 0;

  for (size_t a = 0; a < parsed.arrangement_count; a++) {
    const RsArrangementData *ad = &parsed.arrangements[a];
    for (int i = 0; i < ad->note_count; i++) {
      if (!push_note_expanded(&parsed, ad, i, &buf, &nout, &cap, &group_seq, errmsg)) {
        free(buf);
        rs_sng_parsed_free(&parsed);
        return -1;
      }
    }
  }
  copy_chord_templates_from_parsed(&parsed, out);

  copy_sections_from_parsed(&parsed, out);
  rs_sng_parsed_free(&parsed);

  if (nout == 0) {
    out->notes = NULL;
    out->note_count = 0;
    return 0;
  }

  out->notes = dedupe_track_notes(buf, nout, &out->note_count);
  return 0;
}

/* Merge note rows from every arrangement tier 0..metadata_max_difficulty (Rocksmith
 * stores incremental per-tier blocks; the top tier alone is not the full chart). */
static int parse_and_expand_cumulative_max(const uint8_t *data, size_t len, RsSngPlatform platform, RsChart *out,
                                           char **errmsg) {
  uint8_t *plain = NULL;
  size_t plen = 0;
  int use_mac = (platform == RS_SNG_PLATFORM_MAC);
  if (rs_sng_unpack(data, len, use_mac, &plain, &plen) != 0) {
    if (errmsg) {
      *errmsg = strdup("SNG unpack failed");
    }
    return -1;
  }
  RsSngParsed parsed = {0};
  if (rs_sng_parse_blob(plain, plen, &parsed, 0, errmsg) != 0) {
    free(plain);
    rs_sng_parsed_free(&parsed);
    return -1;
  }
  free(plain);

  if (parsed.arrangement_count == 0) {
    rs_sng_parsed_free(&parsed);
    if (errmsg) {
      *errmsg = strdup("No arrangements in SNG");
    }
    return -1;
  }

  int32_t cap_diff = parsed.metadata_max_difficulty;
  if (cap_diff < 0) {
    cap_diff = 0;
  }

  RsTrackNote *buf = NULL;
  size_t nout = 0, cap = 0;
  int group_seq = 0;

  for (size_t a = 0; a < parsed.arrangement_count; a++) {
    const RsArrangementData *ad = &parsed.arrangements[a];
    if (ad->difficulty > cap_diff) {
      continue;
    }
    for (int i = 0; i < ad->note_count; i++) {
      if (!push_note_expanded(&parsed, ad, i, &buf, &nout, &cap, &group_seq, errmsg)) {
        free(buf);
        rs_sng_parsed_free(&parsed);
        return -1;
      }
    }
  }

  copy_chord_templates_from_parsed(&parsed, out);
  copy_anchors_from_top_difficulties(&parsed, out, RS_ANCHOR_TOP_DIFFICULTY_TIERS);
  prepend_early_anchors_below_top_tiers(&parsed, out, RS_ANCHOR_TOP_DIFFICULTY_TIERS);

  copy_sections_from_parsed(&parsed, out);
  rs_sng_parsed_free(&parsed);

  if (nout == 0) {
    out->notes = NULL;
    out->note_count = 0;
    return 0;
  }

  out->notes = dedupe_track_notes(buf, nout, &out->note_count);
  return 0;
}

int rs_chart_parse_instrument_sng_all_difficulties(const uint8_t *data, size_t len, RsSngPlatform platform,
                                                  RsChart *out_chart, char **errmsg) {
  rs_chart_free(out_chart);
  return parse_and_expand_all(data, len, platform, out_chart, errmsg);
}

int rs_chart_parse_instrument_sng(const uint8_t *data, size_t len, RsSngPlatform platform, RsChart *out_chart,
                                  char **errmsg) {
  rs_chart_free(out_chart);
  return parse_and_expand_cumulative_max(data, len, platform, out_chart, errmsg);
}

int rs_chart_parse_instrument_sng_max_difficulty(const uint8_t *data, size_t len, RsSngPlatform platform,
                                                 RsChart *out_chart, char **errmsg) {
  return rs_chart_parse_instrument_sng(data, len, platform, out_chart, errmsg);
}

int rs_chart_parse_vocal_sng(const uint8_t *data, size_t len, RsSngPlatform platform, RsChart *out_chart,
                            char **errmsg) {
  rs_chart_free(out_chart);
  uint8_t *plain = NULL;
  size_t plen = 0;
  int use_mac = (platform == RS_SNG_PLATFORM_MAC);
  if (rs_sng_unpack(data, len, use_mac, &plain, &plen) != 0) {
    if (errmsg) {
      *errmsg = strdup("SNG unpack failed");
    }
    return -1;
  }
  RsSngParsed parsed = {0};
  if (rs_sng_parse_blob(plain, plen, &parsed, 1, errmsg) != 0) {
    free(plain);
    rs_sng_parsed_free(&parsed);
    return -1;
  }
  free(plain);

  out_chart->vocal_count = (size_t)parsed.vocal_count;
  if (parsed.vocal_count > 0) {
    out_chart->vocals = (RsVocal *)calloc((size_t)parsed.vocal_count, sizeof(RsVocal));
    if (!out_chart->vocals) {
      rs_sng_parsed_free(&parsed);
      if (errmsg) {
        *errmsg = strdup("OOM");
      }
      return -1;
    }
    for (int i = 0; i < parsed.vocal_count; i++) {
      out_chart->vocals[i].time_sec = parsed.vocal_times[i];
      out_chart->vocals[i].length_sec = parsed.vocal_lengths[i];
      out_chart->vocals[i].midi_note = parsed.vocal_notes[i];
      strncpy(out_chart->vocals[i].lyric, parsed.vocal_lyrics[i], sizeof(out_chart->vocals[i].lyric) - 1);
      out_chart->vocals[i].lyric[sizeof(out_chart->vocals[i].lyric) - 1] = '\0';
    }
  }
  copy_sections_from_parsed(&parsed, out_chart);
  rs_sng_parsed_free(&parsed);
  return 0;
}
