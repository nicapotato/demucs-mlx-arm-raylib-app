#ifndef DMX_APP_STATE_H
#define DMX_APP_STATE_H

#include "job_queue.h"
#include "worker_run.h"

#include "platform/threads.h"

#include "raylib.h"

typedef struct {
  DmxJobQueue queue;
  DmxWorkerConfig worker_cfg;
  DmxWorkerLive live;
  DmxThread *worker_thread;
  char output_dir[DMX_PATH_MAX];
  char status_line[DMX_MSG_MAX];
  int model_index; /* 0=htdemucs_6s, 1=htdemucs */
  int write_mp3;
  int cancel_pressed;
  Texture2D logo_tex;
  int logo_loaded;
} DmxApp;

void dmx_app_init(DmxApp *app);
void dmx_app_shutdown(DmxApp *app);
void dmx_app_tick(DmxApp *app);
void dmx_app_add_path(DmxApp *app, const char *path);
void dmx_app_request_cancel(DmxApp *app);
void dmx_app_apply_settings(DmxApp *app);
/* Load wordmark + set window icon after InitWindow / SearchAndSetResourceDir. */
void dmx_app_load_branding(DmxApp *app);

#endif
