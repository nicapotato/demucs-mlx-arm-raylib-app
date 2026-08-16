#include "prefs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#endif

#define DMX_PREFS_DIR_NAME "demucs-mlx-app"
#define DMX_PREFS_FILE_NAME "prefs.conf"
#define DMX_PREFS_KEY "OUTPUT_DIR="

static int prefs_dir(char *out, size_t out_sz) {
  if (!out || out_sz < 8) {
    return -1;
  }
#ifdef _WIN32
  const char *appdata = getenv("APPDATA");
  if (!appdata || !appdata[0]) {
    return -1;
  }
  int n = snprintf(out, out_sz, "%s\\%s", appdata, DMX_PREFS_DIR_NAME);
#else
  const char *home = getenv("HOME");
  if (!home || !home[0]) {
    return -1;
  }
  int n = snprintf(out, out_sz, "%s/Library/Application Support/%s", home, DMX_PREFS_DIR_NAME);
#endif
  if (n < 0 || (size_t)n >= out_sz) {
    return -1;
  }
  return 0;
}

static int prefs_path(char *out, size_t out_sz) {
  char dir[1024];
  if (prefs_dir(dir, sizeof dir) != 0) {
    return -1;
  }
  int n = snprintf(out, out_sz, "%s/%s", dir, DMX_PREFS_FILE_NAME);
  if (n < 0 || (size_t)n >= out_sz) {
    return -1;
  }
  return 0;
}

static int ensure_prefs_dir(void) {
  char dir[1024];
  if (prefs_dir(dir, sizeof dir) != 0) {
    return -1;
  }
#ifdef _WIN32
  if (_mkdir(dir) != 0 && errno != EEXIST) {
    return -1;
  }
#else
  if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
    return -1;
  }
#endif
  return 0;
}

static int path_is_dir(const char *path) {
  struct stat st;
  if (!path || !path[0]) {
    return 0;
  }
  if (stat(path, &st) != 0) {
    return 0;
  }
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

int dmx_prefs_load_output_dir(char *out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return -1;
  }
  out[0] = '\0';

  char path[1024];
  if (prefs_path(path, sizeof path) != 0) {
    return -1;
  }

  FILE *f = fopen(path, "r");
  if (!f) {
    return -1;
  }

  char line[2048];
  int found = 0;
  while (fgets(line, sizeof line, f)) {
    if (strncmp(line, DMX_PREFS_KEY, strlen(DMX_PREFS_KEY)) != 0) {
      continue;
    }
    char *v = line + strlen(DMX_PREFS_KEY);
    while (*v == ' ' || *v == '\t') {
      ++v;
    }
    size_t n = strlen(v);
    while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r' || v[n - 1] == ' ' || v[n - 1] == '\t')) {
      v[--n] = '\0';
    }
    if (n == 0 || n >= out_sz) {
      break;
    }
    memcpy(out, v, n + 1);
    found = 1;
    break;
  }
  fclose(f);

  if (!found || !path_is_dir(out)) {
    out[0] = '\0';
    return -1;
  }
  return 0;
}

int dmx_prefs_save_output_dir(const char *dir) {
  if (!dir || !dir[0]) {
    return -1;
  }
  if (ensure_prefs_dir() != 0) {
    return -1;
  }

  char path[1024];
  if (prefs_path(path, sizeof path) != 0) {
    return -1;
  }

  FILE *f = fopen(path, "w");
  if (!f) {
    return -1;
  }
  fprintf(f, "%s%s\n", DMX_PREFS_KEY, dir);
  fclose(f);
  return 0;
}
