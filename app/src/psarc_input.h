#ifndef DMX_PSARC_INPUT_H
#define DMX_PSARC_INPUT_H

#include <stddef.h>

/* Extract largest WEM from PSARC and decode to a temp WAV.
 * Returns 0 on success; writes absolute wav path into wav_out.
 * On error returns -1 and writes errmsg (caller frees with free()). */
int dmx_psarc_extract_audio(const char *psarc_path, char *wav_out, size_t wav_out_sz, char **errmsg);

#endif
