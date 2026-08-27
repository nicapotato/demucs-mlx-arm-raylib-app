#ifndef RS_SNG_READ_H
#define RS_SNG_READ_H

#include <stddef.h>
#include <stdint.h>

#include "rocksmith_psarc.h"

#define RS_SNG_BEND_MAX 32
#define RS_SNG_NAME_LEN 32

typedef struct {
  float time_sec;
  int fret;
  int width;
} RsSngAnchor;

typedef struct {
  int32_t difficulty;
  int32_t anchor_count;
  RsSngAnchor *anchors;
  int32_t note_count;
  /* Parallel arrays: one SNG "note" row per entry (chords and singles). */
  float *note_times;
  int32_t *note_strings;
  int32_t *note_frets;
  int8_t *note_anchor_frets;
  int8_t *note_anchor_widths;
  float *note_sustains;
  /* Rich fields; same count as note_count. */
  uint32_t *note_masks;
  int32_t *chord_ids;
  int32_t *chord_notes_ids;
  int8_t *pick_directions;
  float *max_bends;
  int *bend_value_counts; /* per note, 0..RS_SNG_BEND_MAX */
  /* Packed bend pairs: for note i, pairs start at bend_pair_offsets[i] in bend_time_sec[] / bend_step[] */
  int *bend_pair_offsets; /* len = note_count + 1 */
  float *bend_time_sec;   /* variable length, shared flat buffer */
  float *bend_step;
} RsArrangementData;

typedef struct {
  uint32_t mask;
  int8_t frets[6];
  int8_t fingers[6];
  int32_t notes[6];
  char name[RS_SNG_NAME_LEN];
} RsChordTemplate;

/* One ChordNotes entry; bend payloads skipped in storage after read (per-string). */
typedef struct {
  uint32_t str_mask[6];
  int8_t slide_to[6];
  int8_t slide_unpitch[6];
  int16_t vibrato[6];
} RsChordNotesRow;

typedef struct {
  int32_t bpm_count;
  float *bpm_times;
  int16_t *bpm_measures;
  int16_t *bpm_beats;
  RsArrangementData *arrangements;
  size_t arrangement_count;
  int32_t metadata_max_difficulty;
  int32_t vocal_count;
  float *vocal_times;
  float *vocal_lengths;
  int32_t *vocal_notes;
  char **vocal_lyrics;
  RsChordTemplate *chords;
  size_t chord_count;
  RsChordNotesRow *chord_note_rows;
  size_t chord_note_count;
  RsSection *sections;
  size_t section_count;
} RsSngParsed;

void rs_sng_parsed_free(RsSngParsed *p);

int rs_sng_parse_blob(const uint8_t *data, size_t len, RsSngParsed *out, int want_vocal_text, char **errmsg);

/* Parse u32 count + N×88-byte section records (test / tooling). Caller frees *out with free(). */
int rs_sng_parse_sections_blob(const uint8_t *data, size_t len, RsSection **out, size_t *out_count, char **errmsg);

#endif
