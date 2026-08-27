#ifndef ROCKSMITH_PSARC_H
#define ROCKSMITH_PSARC_H

#include "rs_sng_mask.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RsPsarc RsPsarc;

typedef enum {
  RS_SNG_PLATFORM_PC = 0,
  RS_SNG_PLATFORM_MAC = 1,
} RsSngPlatform;

#define RNR_PSARC_SOURCE_CDLC 0
#define RNR_PSARC_SOURCE_OFFICIAL 1

/* Optional hook applied to every PSARC path before fopen (rs_psarc_open /
 * rs_psarc_peek_archive_flags). Used on web builds to redirect virtual library
 * paths to staged real files. The resolver returns either `path` unchanged or
 * a replacement written into `buf` (buf_sz bytes). NULL clears the hook. */
typedef const char *(*RsPsarcPathResolver)(const char *path, char *buf, size_t buf_sz);
void rs_psarc_set_path_resolver(RsPsarcPathResolver fn);

/* Read PSARC header archive_flags (offset 28). Returns -1 on error. */
int rs_psarc_peek_archive_flags(const char *path, uint32_t *out_flags);

/* archive_flags == 4 → Official PC DLC; else CDLC. Returns -1 on error. */
int rs_psarc_classify_source(const char *path);

#define RS_TRACK_BEND_CAP 32

/* Per-note chart event (SNG + chord expansion). */
typedef struct {
  float time_sec;
  int string_index;
  int fret;
  float sustain_sec;
  uint32_t mask;
  int8_t pick_direction; /* -1 unset; 0 down; 1 up (RS2014 convention, see SngToXml). */
  float max_bend;
  int bend_value_count;
  float bend_time_sec[RS_TRACK_BEND_CAP];
  float bend_step[RS_TRACK_BEND_CAP];
  int32_t chord_id; /* -1 = single note (not a chord row). */
  int chord_group_id; /* same id for strings expanded from one chord hit; -1 for singles. */
  unsigned char is_from_chord_expansion;
  int8_t anchor_fret; /* SNG per-note hand position; -1 unset. */
  int8_t anchor_width;
  int8_t slide_to; /* pitched slide destination fret; -1 none. */
  int8_t slide_unpitch_to; /* unpitched slide destination fret; -1 none. */
} RsTrackNote;

typedef struct {
  float time_sec;
  float length_sec;
  char lyric[256];
  int midi_note;
} RsVocal;

typedef struct {
  int32_t id;
  int8_t frets[6];
  int8_t fingers[6];
  char name[32];
} RsChordTemplateEntry;

typedef struct {
  float time_sec;
  int fret;
  int width;
} RsAnchor;

/* Song structure marker (intro, verse, chorus, …) from SNG sections block. */
typedef struct {
  char name[32];
  int32_t number;
  float start_time_sec;
} RsSection;

typedef struct {
  RsTrackNote *notes;
  size_t note_count;
  RsAnchor *anchors;
  size_t anchor_count;
  RsVocal *vocals;
  size_t vocal_count;
  RsChordTemplateEntry *chord_templates;
  size_t chord_template_count;
  RsSection *sections;
  size_t section_count;
  char title[256];
} RsChart;

RsPsarc *rs_psarc_open(const char *path, char **errmsg);
void rs_psarc_close(RsPsarc *p);

size_t rs_psarc_file_count(const RsPsarc *p);
const char *rs_psarc_file_name(const RsPsarc *p, size_t index);

uint64_t rs_psarc_file_uncompressed_size(const RsPsarc *p, size_t index);

/* Prefer audio/ *.wem with largest uncompressed size (Rocksmith toolkit style); fallback any .wem. */
int rs_psarc_pick_wem_path(const RsPsarc *p, char *out, size_t out_sz);

/* Temp files for extracted audio (UTF-8 paths). */
int rs_temp_write_bytes(const uint8_t *data, size_t len, const char *suffix, char *path_out, size_t path_out_sz,
                        char **errmsg);
int rs_temp_make_empty_with_suffix(const char *suffix, char *path_out, size_t path_out_sz, char **errmsg);

/* Delete stale rs temp audio files (older than ~1h) left behind by crashes/aborted sessions.
 * Files still open (e.g. a wav playing in another instance) survive: delete fails on Windows,
 * and on POSIX an unlinked open file stays readable. Safe to call at startup. */
void rs_temp_cleanup_orphans(void);

/* Decodes directly from memory (no intermediate temp .wem on disk). */
int rs_audio_wem_decode_bytes_to_temp_wav(const uint8_t *wem_data, size_t wem_len, char *wav_path_out,
                                          size_t wav_path_out_sz, char **errmsg);

int rs_audio_wem_decode_file_to_temp_wav(const char *wem_path, char *wav_path_out, size_t wav_path_out_sz,
                                         char **errmsg);

/* Like rs_audio_wem_decode_file_to_temp_wav, but polls *cancel_flag between render chunks.
 * Returns 0 on success, 1 if cancelled (no wav left on disk, *errmsg untouched), -1 on error. */
int rs_audio_wem_decode_file_to_temp_wav_cancellable(const char *wem_path, char *wav_path_out, size_t wav_path_out_sz,
                                                     const volatile int *cancel_flag, char **errmsg);

int rs_psarc_read_file(RsPsarc *p, const char *path, uint8_t **out_bytes, size_t *out_len, char **errmsg);

int rs_chart_parse_instrument_sng(const uint8_t *data, size_t len, RsSngPlatform platform, RsChart *out_chart,
                                  char **errmsg);

/* Metadata max difficulty only (anchors + chord templates). */
int rs_chart_parse_instrument_sng_max_difficulty(const uint8_t *data, size_t len, RsSngPlatform platform,
                                                 RsChart *out_chart, char **errmsg);

/* Union of every difficulty: chord expansion, techniques, strum pick direction, bends. */
int rs_chart_parse_instrument_sng_all_difficulties(const uint8_t *data, size_t len, RsSngPlatform platform,
                                                   RsChart *out_chart, char **errmsg);

int rs_chart_parse_vocal_sng(const uint8_t *data, size_t len, RsSngPlatform platform, RsChart *out_chart,
                             char **errmsg);

void rs_chart_init(RsChart *c);
void rs_chart_free(RsChart *c);

#ifdef __cplusplus
}
#endif

#endif
