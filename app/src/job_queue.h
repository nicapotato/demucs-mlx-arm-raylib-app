#ifndef DMX_JOB_QUEUE_H
#define DMX_JOB_QUEUE_H

#include <stddef.h>

#define DMX_PATH_MAX 1024
#define DMX_JOB_MAX 64
#define DMX_MSG_MAX 512

typedef enum {
  DMX_JOB_QUEUED = 0,
  DMX_JOB_PREPARING,
  DMX_JOB_RUNNING,
  DMX_JOB_DONE,
  DMX_JOB_ERROR,
  DMX_JOB_CANCELLED,
} DmxJobStatus;

typedef struct {
  char input_path[DMX_PATH_MAX];
  char display_name[256];
  char track_name[256]; /* output subfolder name */
  char prepared_audio[DMX_PATH_MAX]; /* wav fed to worker; may equal input */
  int is_psarc;
  int owns_temp_audio; /* unlink prepared_audio when done */
  DmxJobStatus status;
  float progress_pct;
  char message[DMX_MSG_MAX];
  char out_dir[DMX_PATH_MAX];
} DmxJob;

typedef struct {
  DmxJob jobs[DMX_JOB_MAX];
  int count;
  int active_index; /* -1 if idle */
} DmxJobQueue;

void dmx_queue_init(DmxJobQueue *q);
int dmx_queue_add(DmxJobQueue *q, const char *path);
DmxJob *dmx_queue_next_queued(DmxJobQueue *q, int *out_index);
const char *dmx_job_status_label(DmxJobStatus s);

#endif
