#include "job_queue.h"

#include <stdio.h>
#include <string.h>

void dmx_queue_init(DmxJobQueue *q) {
  memset(q, 0, sizeof *q);
  q->active_index = -1;
}

static void basename_no_ext(const char *path, char *out, size_t out_sz, const char *drop_ext) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  snprintf(out, out_sz, "%s", base);
  size_t n = strlen(out);
  size_t el = strlen(drop_ext);
  if (n > el) {
    for (size_t i = 0; i < el; ++i) {
      char a = out[n - el + i];
      char b = drop_ext[i];
      if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
      }
      if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
      }
      if (a != b) {
        return;
      }
    }
    out[n - el] = '\0';
  }
}

int dmx_queue_add(DmxJobQueue *q, const char *path) {
  if (!q || !path || !path[0] || q->count >= DMX_JOB_MAX) {
    return -1;
  }
  DmxJob *j = &q->jobs[q->count++];
  memset(j, 0, sizeof *j);
  snprintf(j->input_path, sizeof j->input_path, "%s", path);

  const char *dot = strrchr(path, '.');
  j->is_psarc = (dot && (strcmp(dot, ".psarc") == 0 || strcmp(dot, ".PSARC") == 0));
  if (j->is_psarc) {
    basename_no_ext(path, j->track_name, sizeof j->track_name, ".psarc");
  } else {
    basename_no_ext(path, j->track_name, sizeof j->track_name, "");
    /* strip last extension */
    char *d = strrchr(j->track_name, '.');
    if (d) {
      *d = '\0';
    }
  }
  snprintf(j->display_name, sizeof j->display_name, "%s", j->track_name);
  j->status = DMX_JOB_QUEUED;
  snprintf(j->message, sizeof j->message, "Queued");
  return 0;
}

DmxJob *dmx_queue_next_queued(DmxJobQueue *q, int *out_index) {
  if (!q) {
    return NULL;
  }
  for (int i = 0; i < q->count; ++i) {
    if (q->jobs[i].status == DMX_JOB_QUEUED) {
      if (out_index) {
        *out_index = i;
      }
      return &q->jobs[i];
    }
  }
  return NULL;
}

const char *dmx_job_status_label(DmxJobStatus s) {
  switch (s) {
  case DMX_JOB_QUEUED:
    return "Queued";
  case DMX_JOB_PREPARING:
    return "Preparing";
  case DMX_JOB_RUNNING:
    return "Running";
  case DMX_JOB_DONE:
    return "Done";
  case DMX_JOB_ERROR:
    return "Error";
  case DMX_JOB_CANCELLED:
    return "Cancelled";
  }
  return "?";
}
