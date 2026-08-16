/* Cross-platform temp files for extracted OGG / decoded WAV paths.
 *
 * Windows note: GetTempFileNameA's uniqueness guarantee only holds while its .tmp placeholder
 * exists; the old delete-then-rename-extension approach here could hand the same base name to a
 * later call (truncating a wav that was still playing) and raced between the loader and the
 * background WEM decode thread. Names are now generated from pid + tick + an atomic counter and
 * created with CREATE_NEW, the moral equivalent of mkstemps. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "rocksmith_psarc.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#define RS_TEMP_ORPHAN_AGE_SEC (60 * 60)

static void rs_temp_set_errf(char **errmsg, const char *fmt, ...) {
  if (!errmsg) {
    return;
  }
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  *errmsg = strdup(buf);
}

#ifndef _WIN32

/* POSIX file name prefix (also matched by rs_temp_cleanup_orphans). */
#define RS_TEMP_PREFIX "rsaudio"

static const char *rs_temp_dir(void) {
  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir || !tmpdir[0]) {
    tmpdir = "/tmp";
  }
  return tmpdir;
}

static int rs_temp_open_unique(const char *suffix, char *path_out, size_t path_out_sz, char **errmsg) {
  char tpl[768];
  if (snprintf(tpl, sizeof(tpl), "%s/" RS_TEMP_PREFIX "XXXXXX%s", rs_temp_dir(), suffix) >= (int)sizeof(tpl)) {
    rs_temp_set_errf(errmsg, "temp path template too long");
    return -1;
  }
  if (strlen(tpl) >= path_out_sz) {
    rs_temp_set_errf(errmsg, "temp path buffer too small");
    return -1;
  }
  int slen = (int)strlen(suffix);
  int fd = mkstemps(tpl, slen);
  if (fd < 0) {
    rs_temp_set_errf(errmsg, "mkstemps failed (errno=%d)", errno);
    return -1;
  }
  snprintf(path_out, path_out_sz, "%s", tpl);
  return fd;
}

int rs_temp_write_bytes(const uint8_t *data, size_t len, const char *suffix, char *path_out, size_t path_out_sz,
                        char **errmsg) {
  if (errmsg) {
    *errmsg = NULL;
  }
  int fd = rs_temp_open_unique(suffix, path_out, path_out_sz, errmsg);
  if (fd < 0) {
    return -1;
  }
  size_t wr = 0;
  while (wr < len) {
    ssize_t n = write(fd, data + wr, len - wr);
    if (n <= 0) {
      close(fd);
      unlink(path_out);
      rs_temp_set_errf(errmsg, "write temp failed (errno=%d)", errno);
      return -1;
    }
    wr += (size_t)n;
  }
  close(fd);
  return 0;
}

int rs_temp_make_empty_with_suffix(const char *suffix, char *path_out, size_t path_out_sz, char **errmsg) {
  if (errmsg) {
    *errmsg = NULL;
  }
  int fd = rs_temp_open_unique(suffix, path_out, path_out_sz, errmsg);
  if (fd < 0) {
    return -1;
  }
  close(fd);
  return 0;
}

void rs_temp_cleanup_orphans(void) {
  const char *dirpath = rs_temp_dir();
  DIR *d = opendir(dirpath);
  if (!d) {
    return;
  }
  time_t now = time(NULL);
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (strncmp(e->d_name, RS_TEMP_PREFIX, strlen(RS_TEMP_PREFIX)) != 0) {
      continue;
    }
    char full[1024];
    if (snprintf(full, sizeof(full), "%s/%s", dirpath, e->d_name) >= (int)sizeof(full)) {
      continue;
    }
    struct stat st;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
      continue;
    }
    if (now - st.st_mtime < RS_TEMP_ORPHAN_AGE_SEC) {
      continue;
    }
    unlink(full);
  }
  closedir(d);
}

#else /* _WIN32 */

/* Windows file name prefix (also matched by rs_temp_cleanup_orphans). */
#define RS_TEMP_PREFIX "rsa_"

static volatile LONG g_rs_temp_seq;

/* Creates a uniquely named file (CREATE_NEW retry loop) and returns its handle. */
static HANDLE rs_temp_create_unique(const char *suffix, char *path_out, size_t path_out_sz, char **errmsg) {
  char dir[MAX_PATH];
  DWORD dlen = GetTempPathA((DWORD)sizeof(dir), dir);
  if (dlen == 0 || dlen >= sizeof(dir)) {
    rs_temp_set_errf(errmsg, "GetTempPathA failed (err=%lu)", (unsigned long)GetLastError());
    return INVALID_HANDLE_VALUE;
  }
  for (int attempt = 0; attempt < 64; attempt++) {
    LONG seq = InterlockedIncrement(&g_rs_temp_seq);
    char fn[MAX_PATH];
    int n = snprintf(fn, sizeof(fn), "%s" RS_TEMP_PREFIX "%lu_%llu_%ld%s", dir,
                     (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64(), (long)seq, suffix);
    if (n < 0 || n >= (int)sizeof(fn) || (size_t)n >= path_out_sz) {
      rs_temp_set_errf(errmsg, "temp path too long");
      return INVALID_HANDLE_VALUE;
    }
    HANDLE h = CreateFileA(fn, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      snprintf(path_out, path_out_sz, "%s", fn);
      return h;
    }
    DWORD werr = GetLastError();
    if (werr != ERROR_FILE_EXISTS && werr != ERROR_ALREADY_EXISTS) {
      rs_temp_set_errf(errmsg, "CreateFileA temp failed (err=%lu path=%s)", (unsigned long)werr, fn);
      return INVALID_HANDLE_VALUE;
    }
  }
  rs_temp_set_errf(errmsg, "temp name collision retries exhausted");
  return INVALID_HANDLE_VALUE;
}

int rs_temp_write_bytes(const uint8_t *data, size_t len, const char *suffix, char *path_out, size_t path_out_sz,
                        char **errmsg) {
  if (errmsg) {
    *errmsg = NULL;
  }
  HANDLE h = rs_temp_create_unique(suffix, path_out, path_out_sz, errmsg);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }
  size_t wr = 0;
  while (wr < len) {
    DWORD chunk = (len - wr > 0x40000000u) ? 0x40000000u : (DWORD)(len - wr);
    DWORD written = 0;
    if (!WriteFile(h, data + wr, chunk, &written, NULL) || written == 0) {
      DWORD werr = GetLastError();
      CloseHandle(h);
      DeleteFileA(path_out);
      rs_temp_set_errf(errmsg, "write temp failed (err=%lu)", (unsigned long)werr);
      return -1;
    }
    wr += (size_t)written;
  }
  CloseHandle(h);
  return 0;
}

int rs_temp_make_empty_with_suffix(const char *suffix, char *path_out, size_t path_out_sz, char **errmsg) {
  if (errmsg) {
    *errmsg = NULL;
  }
  HANDLE h = rs_temp_create_unique(suffix, path_out, path_out_sz, errmsg);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }
  CloseHandle(h);
  return 0;
}

void rs_temp_cleanup_orphans(void) {
  char dir[MAX_PATH];
  DWORD dlen = GetTempPathA((DWORD)sizeof(dir), dir);
  if (dlen == 0 || dlen >= sizeof(dir)) {
    return;
  }
  char pattern[MAX_PATH];
  if (snprintf(pattern, sizeof(pattern), "%s" RS_TEMP_PREFIX "*", dir) >= (int)sizeof(pattern)) {
    return;
  }
  WIN32_FIND_DATAA fd;
  HANDLE find = FindFirstFileA(pattern, &fd);
  if (find == INVALID_HANDLE_VALUE) {
    return;
  }
  FILETIME now_ft;
  GetSystemTimeAsFileTime(&now_ft);
  ULARGE_INTEGER now_u;
  now_u.LowPart = now_ft.dwLowDateTime;
  now_u.HighPart = now_ft.dwHighDateTime;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue;
    }
    ULARGE_INTEGER mt;
    mt.LowPart = fd.ftLastWriteTime.dwLowDateTime;
    mt.HighPart = fd.ftLastWriteTime.dwHighDateTime;
    /* FILETIME is 100ns units. */
    unsigned long long age_sec = (now_u.QuadPart > mt.QuadPart) ? (now_u.QuadPart - mt.QuadPart) / 10000000ull : 0;
    if (age_sec < (unsigned long long)RS_TEMP_ORPHAN_AGE_SEC) {
      continue;
    }
    char full[MAX_PATH];
    if (snprintf(full, sizeof(full), "%s%s", dir, fd.cFileName) < (int)sizeof(full)) {
      DeleteFileA(full); /* in-use files fail here, which is exactly what we want */
    }
  } while (FindNextFileA(find, &fd));
  FindClose(find);
}

#endif
