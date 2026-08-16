#ifndef DMX_PREFS_H
#define DMX_PREFS_H

#include <stddef.h>

/* Tiny per-user prefs:
 *   macOS: ~/Library/Application Support/demucs-mlx-app/prefs.conf
 *   Windows: %APPDATA%\demucs-mlx-app\prefs.conf
 */

/* Returns 0 on success with a non-empty path written to out. */
int dmx_prefs_load_output_dir(char *out, size_t out_sz);

/* Persist output dir. Returns 0 on success. */
int dmx_prefs_save_output_dir(const char *dir);

#endif
