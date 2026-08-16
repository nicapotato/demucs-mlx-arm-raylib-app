#include "rocksmith_psarc.h"
#include "psarc_crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
  uint8_t md5[16];
  uint32_t zindex_begin;
  uint64_t length;
  uint64_t offset;
  char *name;
} PsarcEntry;

struct RsPsarc {
  FILE *fp;
  uint32_t block_size;
  uint32_t num_files;
  uint32_t total_toc_size;
  uint32_t compression;
  PsarcEntry *entries;
  uint16_t *zblocks;
  size_t zblock_count;
};

static uint32_t be_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static RsPsarcPathResolver g_path_resolver = NULL;

void rs_psarc_set_path_resolver(RsPsarcPathResolver fn) { g_path_resolver = fn; }

/* All PSARC file opens funnel through here so a web build can stage virtual
 * library files (browser File handles) into MEMFS before reading. */
static FILE *psarc_fopen(const char *path) {
  char rbuf[1024];
  if (g_path_resolver) {
    path = g_path_resolver(path, rbuf, sizeof rbuf);
    if (!path) {
      return NULL;
    }
  }
  return fopen(path, "rb");
}

int rs_psarc_peek_archive_flags(const char *path, uint32_t *out_flags) {
  if (!path || !out_flags) {
    return -1;
  }
  FILE *fp = psarc_fopen(path);
  if (!fp) {
    return -1;
  }
  uint8_t hdr[32];
  if (fread(hdr, 1, 32, fp) != 32) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  if (be_u32(hdr) != 0x50534152) {
    return -1;
  }
  *out_flags = be_u32(hdr + 28);
  return 0;
}

int rs_psarc_classify_source(const char *path) {
  uint32_t flags = 0;
  if (rs_psarc_peek_archive_flags(path, &flags) != 0) {
    return -1;
  }
  if (flags != 4u) {
    return RNR_PSARC_SOURCE_CDLC;
  }

  char *err = NULL;
  RsPsarc *p = rs_psarc_open(path, &err);
  if (!p) {
    free(err);
    return RNR_PSARC_SOURCE_OFFICIAL;
  }

  int source = RNR_PSARC_SOURCE_OFFICIAL;
  size_t n = rs_psarc_file_count(p);
  for (size_t i = 0; i < n; i++) {
    const char *nm = rs_psarc_file_name(p, i);
    if (!nm || !nm[0]) {
      continue;
    }
    if (strstr(nm, "toolkit.version") != NULL) {
      source = RNR_PSARC_SOURCE_CDLC;
      break;
    }
    if (strstr(nm, "songs/arr/") != NULL || strstr(nm, "songs\\arr\\") != NULL) {
      if (strstr(nm, "_lead.xml") != NULL || strstr(nm, "_rhythm.xml") != NULL || strstr(nm, "_bass.xml") != NULL) {
        source = RNR_PSARC_SOURCE_CDLC;
        break;
      }
    }
    if (strstr(nm, ".ogg") != NULL || strstr(nm, ".OGG") != NULL) {
      source = RNR_PSARC_SOURCE_CDLC;
      break;
    }
  }

  rs_psarc_close(p);
  return source;
}

static uint64_t be_u40(const uint8_t *p) {
  return ((uint64_t)p[0] << 32) | ((uint64_t)p[1] << 24) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 8) | (uint64_t)p[4];
}

static uint16_t be_u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int inflate_zlib_chunk(const uint8_t *src, size_t src_len, uint8_t **dst, size_t *dst_cap, size_t *dst_len) {
  size_t need = *dst_len + (size_t)compressBound((uLong)src_len) + 256u;
  if (need > *dst_cap) {
    uint8_t *nb = (uint8_t *)realloc(*dst, need);
    if (!nb) {
      return -1;
    }
    *dst = nb;
    *dst_cap = need;
  }
  uLongf dest_len = (uLongf)(*dst_cap - *dst_len);
  uint8_t *wp = *dst + *dst_len;
  int st = uncompress(wp, &dest_len, src, (uLong)src_len);
  if (st != Z_OK) {
    return -1;
  }
  *dst_len += (size_t)dest_len;
  return 0;
}

static int psarc_inflate_entry(RsPsarc *p, size_t ent_idx, uint8_t **out, size_t *out_len) {
  if (ent_idx >= p->num_files) {
    return -1;
  }
  PsarcEntry *e = &p->entries[ent_idx];
  size_t target = (size_t)e->length;
  uint32_t zid = e->zindex_begin;
  int bs = (int)p->block_size;
  size_t cap = target ? target : 65536;
  if (cap < 65536) {
    cap = 65536;
  }
  uint8_t *buf = (uint8_t *)malloc(cap);
  if (!buf) {
    return -1;
  }
  size_t got = 0;
  long file_off = (long)e->offset;
  if (fseek(p->fp, file_off, SEEK_SET) != 0) {
    free(buf);
    return -1;
  }
  while (got < target) {
    if ((size_t)zid >= p->zblock_count) {
      free(buf);
      return -1;
    }
    uint32_t zlen = p->zblocks[zid];
    if (zlen == 0) {
      uint8_t raw[65536];
      if (bs > (int)sizeof(raw)) {
        free(buf);
        return -1;
      }
      if (fread(raw, 1, (size_t)bs, p->fp) != (size_t)bs) {
        free(buf);
        return -1;
      }
      if (got + (size_t)bs > cap) {
        cap = got + (size_t)bs + 65536;
        uint8_t *nb = (uint8_t *)realloc(buf, cap);
        if (!nb) {
          free(buf);
          return -1;
        }
        buf = nb;
      }
      memcpy(buf + got, raw, (size_t)bs);
      got += (size_t)bs;
    } else {
      uint8_t *chunk = (uint8_t *)malloc(zlen);
      if (!chunk) {
        free(buf);
        return -1;
      }
      if (fread(chunk, 1, zlen, p->fp) != zlen) {
        free(chunk);
        free(buf);
        return -1;
      }
      uint16_t magic = be_u16(chunk);
      int is_zlib = (magic == 0x789C || magic == 0x78DA);
      if (is_zlib) {
        size_t add_cap = cap;
        if (inflate_zlib_chunk(chunk, zlen, &buf, &add_cap, &got)) {
          free(chunk);
          free(buf);
          return -1;
        }
        cap = add_cap;
      } else {
        if (got + zlen > cap) {
          cap = got + zlen + 65536;
          uint8_t *nb = (uint8_t *)realloc(buf, cap);
          if (!nb) {
            free(chunk);
            free(buf);
            return -1;
          }
          buf = nb;
        }
        memcpy(buf + got, chunk, zlen);
        got += zlen;
      }
      free(chunk);
    }
    zid++;
  }
  *out = buf;
  *out_len = got;
  return 0;
}

static void split_names(const char *text, RsPsarc *p) {
  const char *s = text;
  size_t line = 0;
  while (*s && line + 1u < p->num_files) {
    const char *nl = strchr(s, '\n');
    size_t ln = nl ? (size_t)(nl - s) : strlen(s);
    size_t entry_idx = line + 1u;
    free(p->entries[entry_idx].name);
    p->entries[entry_idx].name = (char *)malloc(ln + 1);
    if (p->entries[entry_idx].name) {
      memcpy(p->entries[entry_idx].name, s, ln);
      p->entries[entry_idx].name[ln] = '\0';
    }
    line++;
    if (!nl) {
      break;
    }
    s = nl + 1;
  }
}

static int load_manifest_names(RsPsarc *p, char **errmsg) {
  uint8_t *raw = NULL;
  size_t rawlen = 0;
  if (psarc_inflate_entry(p, 0, &raw, &rawlen)) {
    if (errmsg) {
      *errmsg = strdup("Failed to inflate NamesBlock");
    }
    return -1;
  }
  char *txt = (char *)malloc(rawlen + 1);
  if (!txt) {
    free(raw);
    if (errmsg) {
      *errmsg = strdup("OOM");
    }
    return -1;
  }
  memcpy(txt, raw, rawlen);
  txt[rawlen] = '\0';
  free(raw);
  split_names(txt, p);
  free(txt);
  return 0;
}

RsPsarc *rs_psarc_open(const char *path, char **errmsg) {
  FILE *fp = psarc_fopen(path);
  if (!fp) {
    if (errmsg) {
      *errmsg = strdup("Cannot open PSARC");
    }
    return NULL;
  }
  uint8_t hdr[32];
  if (fread(hdr, 1, 32, fp) != 32) {
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("PSARC too small");
    }
    return NULL;
  }
  uint32_t magic = be_u32(hdr);
  if (magic != 0x50534152) {
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("Not a PSAR file");
    }
    return NULL;
  }
  uint32_t total_toc = be_u32(hdr + 12);
  uint32_t toc_entry_size = be_u32(hdr + 16);
  uint32_t num_files = be_u32(hdr + 20);
  uint32_t block_size = be_u32(hdr + 24);
  uint32_t arch_flags = be_u32(hdr + 28);
  (void)toc_entry_size;
  uint32_t compression = be_u32(hdr + 8);
  if (compression != 0x7A6C6962) {
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("Unsupported PSARC compression");
    }
    return NULL;
  }

  int bnum = 2;
  if (block_size == 65536) {
    bnum = 2;
  } else {
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("Unexpected block size");
    }
    return NULL;
  }

  size_t toc_bytes = (size_t)total_toc - 32u;
  uint8_t *tocbuf = (uint8_t *)malloc(toc_bytes);
  if (!tocbuf) {
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("OOM");
    }
    return NULL;
  }
  if (fread(tocbuf, 1, toc_bytes, fp) != toc_bytes) {
    free(tocbuf);
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("Read TOC failed");
    }
    return NULL;
  }

  if (arch_flags == 4u) {
    uint8_t *plain = (uint8_t *)malloc(toc_bytes);
    if (!plain) {
      free(tocbuf);
      fclose(fp);
      if (errmsg) {
        *errmsg = strdup("OOM");
      }
      return NULL;
    }
    if (rs_psarc_decrypt_toc_region(tocbuf, toc_bytes, plain) != 0) {
      free(plain);
      free(tocbuf);
      fclose(fp);
      if (errmsg) {
        *errmsg = strdup("PSARC TOC decrypt failed");
      }
      return NULL;
    }
    free(tocbuf);
    tocbuf = plain;
  }

  RsPsarc *p = (RsPsarc *)calloc(1, sizeof(RsPsarc));
  if (!p) {
    free(tocbuf);
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("OOM");
    }
    return NULL;
  }
  p->fp = fp;
  p->block_size = block_size;
  p->num_files = num_files;
  p->total_toc_size = total_toc;
  p->compression = compression;
  p->entries = (PsarcEntry *)calloc(num_files, sizeof(PsarcEntry));
  if (!p->entries) {
    free(tocbuf);
    free(p);
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("OOM");
    }
    return NULL;
  }

  const uint8_t *q = tocbuf;
  for (uint32_t i = 0; i < num_files; i++) {
    memcpy(p->entries[i].md5, q, 16);
    q += 16;
    p->entries[i].zindex_begin = be_u32(q);
    q += 4;
    p->entries[i].length = be_u40(q);
    q += 5;
    p->entries[i].offset = be_u40(q);
    q += 5;
  }
  size_t toc_chunk = (size_t)num_files * 30u;
  if (toc_bytes < toc_chunk) {
    free(tocbuf);
    free(p->entries);
    free(p);
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("Corrupt TOC");
    }
    return NULL;
  }
  size_t znum = (toc_bytes - toc_chunk) / (size_t)bnum;
  p->zblock_count = znum;
  p->zblocks = (uint16_t *)malloc(znum * sizeof(uint16_t));
  if (!p->zblocks) {
    free(tocbuf);
    free(p->entries);
    free(p);
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("OOM");
    }
    return NULL;
  }
  for (size_t i = 0; i < znum; i++) {
    p->zblocks[i] = be_u16(q);
    q += 2;
  }
  free(tocbuf);

  if (fseek(fp, (long)total_toc, SEEK_SET) != 0) {
    free(p->zblocks);
    free(p->entries);
    free(p);
    fclose(fp);
    if (errmsg) {
      *errmsg = strdup("Seek PSARC data");
    }
    return NULL;
  }

  if (load_manifest_names(p, errmsg)) {
    free(p->zblocks);
    free(p->entries);
    free(p);
    fclose(fp);
    return NULL;
  }

  return p;
}

void rs_psarc_close(RsPsarc *p) {
  if (!p) {
    return;
  }
  if (p->fp) {
    fclose(p->fp);
  }
  if (p->entries) {
    for (uint32_t i = 0; i < p->num_files; i++) {
      free(p->entries[i].name);
    }
    free(p->entries);
  }
  free(p->zblocks);
  free(p);
}

size_t rs_psarc_file_count(const RsPsarc *p) { return p ? (size_t)p->num_files : 0; }

const char *rs_psarc_file_name(const RsPsarc *p, size_t index) {
  if (!p || index >= p->num_files) {
    return "";
  }
  return p->entries[index].name ? p->entries[index].name : "";
}


uint64_t rs_psarc_file_uncompressed_size(const RsPsarc *p, size_t index) {
  if (!p || index >= p->num_files) {
    return 0;
  }
  return p->entries[index].length;
}

static int name_ends_wem_ci(const char *nm) {
  size_t n = strlen(nm);
  if (n < 4) {
    return 0;
  }
  const char *s = nm + (n - 4);
  return (s[0] == '.') && ((s[1] == 'w' || s[1] == 'W') && (s[2] == 'e' || s[2] == 'E') &&
                             (s[3] == 'm' || s[3] == 'M'));
}

int rs_psarc_pick_wem_path(const RsPsarc *p, char *out, size_t out_sz) {
  if (!p || !out || out_sz == 0) {
    return -1;
  }
  out[0] = '\0';
  size_t nfiles = (size_t)p->num_files;

  for (size_t pass = 0; pass < 2; pass++) {
    size_t best_i = (size_t)-1;
    uint64_t best_len = 0;
    const char *best_name = NULL;

    for (size_t i = 0; i < nfiles; i++) {
      const char *nm = p->entries[i].name;
      if (!nm || !nm[0]) {
        continue;
      }
      if (!name_ends_wem_ci(nm)) {
        continue;
      }
      if (pass == 0 && strncmp(nm, "audio/", 6) != 0) {
        continue;
      }
      uint64_t len = p->entries[i].length;
      if (best_i == (size_t)-1 || len > best_len ||
          (len == best_len && best_name && strcmp(nm, best_name) < 0)) {
        best_i = i;
        best_len = len;
        best_name = nm;
      }
    }

    if (best_i != (size_t)-1 && best_i < nfiles) {
      const char *pick = p->entries[best_i].name;
      if (!pick) {
        return -1;
      }
      strncpy(out, pick, out_sz - 1);
      out[out_sz - 1] = '\0';
      return 0;
    }
  }

  return -1;
}

static int path_match(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a;
    char cb = *b;
    if (ca == '\\') {
      ca = '/';
    }
    if (cb == '\\') {
      cb = '/';
    }
    if (ca != cb) {
      return 0;
    }
    a++;
    b++;
  }
  return *a == *b;
}

int rs_psarc_read_file(RsPsarc *p, const char *path, uint8_t **out_bytes, size_t *out_len, char **errmsg) {
  if (!p || !path || !out_bytes || !out_len) {
    return -1;
  }
  *out_bytes = NULL;
  *out_len = 0;
  for (uint32_t i = 0; i < p->num_files; i++) {
    const char *nm = p->entries[i].name;
    if (!nm) {
      continue;
    }
    if (path_match(nm, path)) {
      return psarc_inflate_entry(p, i, out_bytes, out_len);
    }
  }
  if (errmsg) {
    *errmsg = strdup("File not found in PSARC");
  }
  return -1;
}
