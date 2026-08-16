#include "rocksmith_psarc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ends_with(const char *s, const char *suffix) {
  size_t ls = strlen(s), lx = strlen(suffix);
  if (ls < lx) {
    return 0;
  }
  return strcmp(s + ls - lx, suffix) == 0;
}

static int in_songs_bin(const char *nm) {
  return strstr(nm, "songs/bin") != NULL || strstr(nm, "songs\\bin") != NULL;
}

static const char *label_for_sng(const char *nm) {
  if (!ends_with(nm, ".sng") || !in_songs_bin(nm)) {
    return NULL;
  }
  if (strstr(nm, "lead")) {
    return "Lead";
  }
  if (strstr(nm, "rhythm")) {
    return "Rhythm";
  }
  if (strstr(nm, "bass")) {
    return "Bass";
  }
  if (strstr(nm, "vocals")) {
    return "Vocals";
  }
  return "Other";
}

static void print_instrument_list(RsPsarc *p) {
  printf("  Files in archive (songs/bin *.sng):\n");
  size_t n = rs_psarc_file_count(p);
  int any = 0;
  for (size_t i = 0; i < n; i++) {
    const char *nm = rs_psarc_file_name(p, i);
    if (!nm || !ends_with(nm, ".sng") || !in_songs_bin(nm)) {
      continue;
    }
    const char *lab = label_for_sng(nm);
    printf("    [%s] %s\n", lab ? lab : "?", nm);
    any = 1;
  }
  if (!any) {
    printf("    (none matched)\n");
  }
}

static void sample_notes_middle(const RsChart *ch, const char *label, size_t max_show) {
  if (!ch->note_count) {
    printf("    (%s: no notes)\n", label);
    return;
  }
  float t_first = ch->notes[0].time_sec;
  float t_last = ch->notes[ch->note_count - 1].time_sec;
  float span = t_last - t_first;
  float mid = (t_first + t_last) * 0.5f;
  printf("    %s: %zu notes, time [%.2fs .. %.2fs], mid=%.2fs\n", label, ch->note_count, t_first, t_last, mid);

  size_t mid_idx = ch->note_count / 2;
  size_t start = (mid_idx > max_show / 2) ? mid_idx - max_show / 2 : 0;
  printf("    sample notes around middle (index %zu..):\n", start);
  for (size_t k = 0; k < max_show && start + k < ch->note_count; k++) {
    const RsTrackNote *n = &ch->notes[start + k];
    printf("      t=%7.3fs  str=%d  fret=%2d  sus=%.3fs  m=0x%08X  pick=%d  chord=%d grp=%d\n", n->time_sec,
           n->string_index, n->fret, n->sustain_sec, n->mask, (int)n->pick_direction, (int)n->chord_id,
           n->chord_group_id);
  }
  (void)span;
}

static void sample_vocals_middle(const RsChart *ch, size_t max_show) {
  if (!ch->vocal_count) {
    printf("    (Vocals: no events)\n");
    return;
  }
  float t_first = ch->vocals[0].time_sec;
  float t_last = ch->vocals[ch->vocal_count - 1].time_sec;
  float mid = (t_first + t_last) * 0.5f;
  printf("    Vocals: %zu events, time [%.2fs .. %.2fs], mid=%.2fs\n", ch->vocal_count, t_first, t_last, mid);
  size_t mid_idx = ch->vocal_count / 2;
  size_t start = (mid_idx > max_show / 2) ? mid_idx - max_show / 2 : 0;
  printf("    sample lyrics around middle:\n");
  for (size_t k = 0; k < max_show && start + k < ch->vocal_count; k++) {
    const RsVocal *v = &ch->vocals[start + k];
    printf("      t=%7.3fs  len=%.3fs  note=%d  \"%s\"\n", v->time_sec, v->length_sec, v->midi_note, v->lyric);
  }
}

static int try_parse_instrument(RsPsarc *p, const char *path, const char *label) {
  uint8_t *buf = NULL;
  size_t len = 0;
  char *err = NULL;
  if (rs_psarc_read_file(p, path, &buf, &len, &err)) {
    printf("    [%s] read failed: %s\n", label, err ? err : "?");
    free(err);
    return -1;
  }
  RsChart ch = {0};
  if (rs_chart_parse_instrument_sng_max_difficulty(buf, len, RS_SNG_PLATFORM_PC, &ch, &err)) {
    printf("    [%s] parse failed: %s\n", label, err ? err : "?");
    free(err);
    free(buf);
    return -1;
  }
  free(buf);
  printf("    %s: %zu chart anchors, %zu song sections\n", label, ch.anchor_count, ch.section_count);
  if (ch.section_count > 0 && ch.section_count <= 6) {
    for (size_t si = 0; si < ch.section_count; si++) {
      printf("      section[%zu] name=%s number=%d t=%.3fs\n", si, ch.sections[si].name, (int)ch.sections[si].number,
             ch.sections[si].start_time_sec);
    }
  } else if (ch.section_count > 6) {
    for (size_t si = 0; si < 3; si++) {
      printf("      section[%zu] name=%s number=%d t=%.3fs\n", si, ch.sections[si].name, (int)ch.sections[si].number,
             ch.sections[si].start_time_sec);
    }
    printf("      ... (%zu total)\n", ch.section_count);
  }
  if (ch.anchor_count > 0 && ch.anchor_count <= 8) {
    for (size_t ai = 0; ai < ch.anchor_count; ai++) {
      printf("      anchor[%zu] t=%.3fs fret=%d width=%d\n", ai, ch.anchors[ai].time_sec, ch.anchors[ai].fret,
             ch.anchors[ai].width);
    }
  } else if (ch.anchor_count > 8) {
    for (size_t ai = 0; ai < 4; ai++) {
      printf("      anchor[%zu] t=%.3fs fret=%d width=%d\n", ai, ch.anchors[ai].time_sec, ch.anchors[ai].fret,
             ch.anchors[ai].width);
    }
    printf("      ... (%zu total)\n", ch.anchor_count);
  }
  sample_notes_middle(&ch, label, 8);
  rs_chart_free(&ch);
  return 0;
}

static int try_parse_vocal(RsPsarc *p, const char *path) {
  uint8_t *buf = NULL;
  size_t len = 0;
  char *err = NULL;
  if (rs_psarc_read_file(p, path, &buf, &len, &err)) {
    printf("    [Vocals] read failed: %s\n", err ? err : "?");
    free(err);
    return -1;
  }
  RsChart ch = {0};
  if (rs_chart_parse_vocal_sng(buf, len, RS_SNG_PLATFORM_PC, &ch, &err)) {
    printf("    [Vocals] parse failed: %s\n", err ? err : "?");
    free(err);
    free(buf);
    return -1;
  }
  free(buf);
  sample_vocals_middle(&ch, 8);
  rs_chart_free(&ch);
  return 0;
}

static const char *pick_first(RsPsarc *p, int (*pred)(const char *)) {
  size_t n = rs_psarc_file_count(p);
  const char *best = NULL;
  for (size_t i = 0; i < n; i++) {
    const char *nm = rs_psarc_file_name(p, i);
    if (!nm) {
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

static int is_lead(const char *nm) {
  return ends_with(nm, ".sng") && strstr(nm, "lead") && in_songs_bin(nm);
}
static int is_rhythm(const char *nm) {
  return ends_with(nm, ".sng") && strstr(nm, "rhythm") && in_songs_bin(nm);
}
static int is_bass(const char *nm) {
  return ends_with(nm, ".sng") && strstr(nm, "bass") && in_songs_bin(nm);
}
static int is_vocals(const char *nm) {
  return ends_with(nm, ".sng") && strstr(nm, "vocals") && in_songs_bin(nm);
}

/* Regression: max-difficulty parse must merge tiers 0..N, not only the top block. */
static int test_deadwing_lead_cumulative(const char *psarc_path, RsPsarc *p) {
  if (!strstr(psarc_path, "Deadwing")) {
    return 0;
  }
  const char *lead = pick_first(p, is_lead);
  if (!lead) {
    fprintf(stderr, "regression: Deadwing lead.sng not found\n");
    return 1;
  }
  uint8_t *buf = NULL;
  size_t len = 0;
  char *err = NULL;
  if (rs_psarc_read_file(p, lead, &buf, &len, &err)) {
    fprintf(stderr, "regression: lead read failed: %s\n", err ? err : "?");
    free(err);
    return 1;
  }
  RsChart ch = {0};
  if (rs_chart_parse_instrument_sng_max_difficulty(buf, len, RS_SNG_PLATFORM_PC, &ch, &err) != 0) {
    fprintf(stderr, "regression: max-difficulty parse failed: %s\n", err ? err : "?");
    free(err);
    free(buf);
    return 1;
  }
  free(buf);
  int fail = 0;
  if (ch.note_count < 500) {
    fprintf(stderr, "regression: Deadwing lead note_count=%zu (expected >500)\n", ch.note_count);
    fail = 1;
  }
  if (ch.note_count > 0) {
    float t0 = ch.notes[0].time_sec;
    float t1 = ch.notes[ch.note_count - 1].time_sec;
    if (t0 >= 60.0f) {
      fprintf(stderr, "regression: Deadwing lead first note t=%.2f (expected <60)\n", t0);
      fail = 1;
    }
    if (t1 <= 400.0f) {
      fprintf(stderr, "regression: Deadwing lead last note t=%.2f (expected >400)\n", t1);
      fail = 1;
    }
    if (!fail) {
      printf("  regression OK: Deadwing lead cumulative max %zu notes [%.2f .. %.2f]\n", ch.note_count, t0, t1);
    }
    if (ch.anchor_count > 0) {
      float a0 = ch.anchors[0].time_sec;
      float a1 = ch.anchors[ch.anchor_count - 1].time_sec;
      printf("  anchors (top-2 + early gap): %zu [%.2f .. %.2f] first fret=%d\n", ch.anchor_count, a0, a1,
             ch.anchors[0].fret);
      if (a0 >= 120.0f) {
        fprintf(stderr, "regression: Deadwing lead first anchor t=%.2f (expected <120)\n", a0);
        fail = 1;
      }
    }
  }
  rs_chart_free(&ch);
  return fail;
}

static void process_psarc(const char *path) {
  char *err = NULL;
  printf("\n========== %s ==========\n", path);
  RsPsarc *p = rs_psarc_open(path, &err);
  if (!p) {
    printf("ERROR: %s\n", err ? err : "open failed");
    free(err);
    return;
  }
  print_instrument_list(p);

  const char *lead = pick_first(p, is_lead);
  const char *rhythm = pick_first(p, is_rhythm);
  const char *bass = pick_first(p, is_bass);
  const char *voc = pick_first(p, is_vocals);

  printf("  Parsed charts (max difficulty), samples from middle of timeline:\n");
  if (lead) {
    try_parse_instrument(p, lead, "Lead");
  }
  if (rhythm) {
    try_parse_instrument(p, rhythm, "Rhythm");
  }
  if (bass) {
    try_parse_instrument(p, bass, "Bass");
  }
  if (voc) {
    try_parse_vocal(p, voc);
  }

  int regress_fail = test_deadwing_lead_cumulative(path, p);
  rs_psarc_close(p);
  if (regress_fail) {
    exit(1);
  }
}

int main(int argc, char **argv) {
  const char *a =
      argc > 1 ? argv[1]
               : "/Users/nicapotato/Documents/repo/apps/rocknroller/cdlc/psarc/"
                 "Led-Zeppelin_Stairway-to-Heaven_v3_0RM_p.psarc";
  const char *b = argc > 2 ? argv[2] : a;

  process_psarc(a);
  process_psarc(b);
  return 0;
}
