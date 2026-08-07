#include "app_state.h"

#include "prefs.h"
#include "psarc_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  DmxApp *app;
  int job_index;
} DmxWorkerArgs;

static int is_supported_path(const char *path) {
  const char *dot = strrchr(path, '.');
  if (!dot) {
    return 0;
  }
  static const char *exts[] = {".mp3", ".wav", ".ogg", ".flac", ".m4a", ".aiff", ".aif", ".psarc", NULL};
  for (int i = 0; exts[i]; ++i) {
    const char *a = dot;
    const char *b = exts[i];
    int ok = 1;
    while (*a && *b) {
      char ca = *a;
      char cb = *b;
      if (ca >= 'A' && ca <= 'Z') {
        ca = (char)(ca - 'A' + 'a');
      }
      if (ca != cb) {
        ok = 0;
        break;
      }
      ++a;
      ++b;
    }
    if (ok && *a == '\0' && *b == '\0') {
      return 1;
    }
  }
  return 0;
}

int dmx_app_queued_count(const DmxApp *app) {
  if (!app) {
    return 0;
  }
  int n = 0;
  for (int i = 0; i < app->queue.count; ++i) {
    if (app->queue.jobs[i].status == DMX_JOB_QUEUED) {
      ++n;
    }
  }
  return n;
}

void dmx_app_init(DmxApp *app) {
  memset(app, 0, sizeof *app);
  dmx_queue_init(&app->queue);
  dmx_worker_config_defaults(&app->worker_cfg);
  dmx_worker_live_reset(&app->live);
  app->model_index = 0;
  app->write_mp3 = 1;
  app->output_dir[0] = '\0';
  app->output_dir_accepted = 0;
  app->processing_enabled = 0;

  char err[512];
  if (dmx_worker_resolve(&app->worker_cfg, err, sizeof err) != 0) {
    snprintf(app->status_line, sizeof app->status_line, "%s", err);
  } else {
    snprintf(app->status_line, sizeof app->status_line,
             "Add files, pick an output folder, then press Start");
  }

  /* Restore last output folder if it still exists on disk. */
  char saved[DMX_PATH_MAX];
  if (dmx_prefs_load_output_dir(saved, sizeof saved) == 0) {
    snprintf(app->output_dir, sizeof app->output_dir, "%s", saved);
    app->output_dir_accepted = 1;
    snprintf(app->status_line, sizeof app->status_line, "Output restored -> %s", saved);
  }

  dmx_app_apply_settings(app);
}

static Image dmx_load_image_branding(const char *name) {
  Image img = LoadImage(name);
  if (img.data == NULL) {
    img = LoadImage(TextFormat("%sresources/%s", GetApplicationDirectory(), name));
  }
  if (img.data == NULL) {
    img = LoadImage(TextFormat("resources/%s", name));
  }
  return img;
}

void dmx_app_load_branding(DmxApp *app) {
  if (!app) {
    return;
  }

  Image logo = dmx_load_image_branding("demuc-mlx-app-logo-export-1000.png");
  if (logo.data != NULL) {
    app->logo_tex = LoadTextureFromImage(logo);
    SetTextureFilter(app->logo_tex, TEXTURE_FILTER_POINT);
    app->logo_loaded = 1;
    UnloadImage(logo);
  }

  Image win_icon = dmx_load_image_branding("demucs-mlx-app-initials.png");
  if (win_icon.data != NULL) {
    SetWindowIcon(win_icon);
    UnloadImage(win_icon);
  }
}

void dmx_app_shutdown(DmxApp *app) {
  if (!app) {
    return;
  }
  if (app->live.running) {
    dmx_worker_request_cancel(&app->live);
  }
  if (app->worker_thread) {
    dmx_thread_join(app->worker_thread);
    app->worker_thread = NULL;
  }
  if (app->logo_loaded) {
    UnloadTexture(app->logo_tex);
    app->logo_loaded = 0;
  }
}

void dmx_app_apply_settings(DmxApp *app) {
  if (app->model_index == 0) {
    snprintf(app->worker_cfg.model_name, sizeof app->worker_cfg.model_name, "htdemucs_6s");
  } else {
    snprintf(app->worker_cfg.model_name, sizeof app->worker_cfg.model_name, "htdemucs");
  }
  app->worker_cfg.write_mp3 = app->write_mp3;
}

void dmx_app_set_output_dir(DmxApp *app, const char *dir) {
  if (!app || !dir || !dir[0]) {
    return;
  }
  snprintf(app->output_dir, sizeof app->output_dir, "%s", dir);
  app->output_dir_accepted = 1;
  if (dmx_prefs_save_output_dir(dir) != 0) {
    snprintf(app->status_line, sizeof app->status_line,
             "Output set (could not save prefs): %s", dir);
    return;
  }
  int q = dmx_app_queued_count(app);
  if (q > 0 && !app->processing_enabled) {
    snprintf(app->status_line, sizeof app->status_line, "Output set - %d queued, press Start", q);
  } else {
    snprintf(app->status_line, sizeof app->status_line, "Output -> %s", dir);
  }
}

void dmx_app_add_path(DmxApp *app, const char *path) {
  if (!app || !path || !path[0]) {
    return;
  }
  if (!is_supported_path(path)) {
    snprintf(app->status_line, sizeof app->status_line, "Unsupported: %s", path);
    return;
  }
  if (dmx_queue_add(&app->queue, path) != 0) {
    snprintf(app->status_line, sizeof app->status_line, "Queue full");
    return;
  }
  int q = dmx_app_queued_count(app);
  if (!app->output_dir_accepted || !app->output_dir[0]) {
    snprintf(app->status_line, sizeof app->status_line,
             "Queued %d file(s) - pick output folder, then Start", q);
  } else if (!app->processing_enabled) {
    snprintf(app->status_line, sizeof app->status_line, "Queued %d file(s) - press Start", q);
  } else {
    snprintf(app->status_line, sizeof app->status_line, "Added %s", path);
  }
}

void dmx_app_start(DmxApp *app) {
  if (!app) {
    return;
  }
  if (!app->output_dir_accepted || !app->output_dir[0]) {
    snprintf(app->status_line, sizeof app->status_line, "Pick an output folder first");
    return;
  }
  if (dmx_app_queued_count(app) == 0 && !app->worker_thread) {
    snprintf(app->status_line, sizeof app->status_line, "Add files before pressing Start");
    return;
  }
  app->processing_enabled = 1;
  app->cancel_pressed = 0;
  snprintf(app->status_line, sizeof app->status_line, "Processing...");
}

void dmx_app_stop(DmxApp *app) {
  if (!app) {
    return;
  }
  app->processing_enabled = 0;
  app->cancel_pressed = 1;
  dmx_worker_request_cancel(&app->live);
  snprintf(app->status_line, sizeof app->status_line, "Stopped");
}

void dmx_app_request_cancel(DmxApp *app) {
  dmx_app_stop(app);
}

static void cleanup_temp(DmxJob *job) {
  if (job && job->owns_temp_audio && job->prepared_audio[0]) {
    unlink(job->prepared_audio);
    job->prepared_audio[0] = '\0';
    job->owns_temp_audio = 0;
  }
}

static void *worker_thread_main(void *arg) {
  DmxWorkerArgs *wa = (DmxWorkerArgs *)arg;
  DmxApp *app = wa->app;
  DmxJob *job = &app->queue.jobs[wa->job_index];

  snprintf(job->out_dir, sizeof job->out_dir, "%s", app->output_dir);
  job->status = DMX_JOB_PREPARING;
  snprintf(job->message, sizeof job->message, "Preparing...");
  snprintf(app->live.status_msg, sizeof app->live.status_msg, "Preparing...");

  if (job->is_psarc) {
    char *errmsg = NULL;
    if (dmx_psarc_extract_audio(job->input_path, job->prepared_audio, sizeof job->prepared_audio, &errmsg) !=
        0) {
      job->status = DMX_JOB_ERROR;
      snprintf(job->message, sizeof job->message, "%s", errmsg ? errmsg : "psarc extract failed");
      snprintf(app->live.error_msg, sizeof app->live.error_msg, "%s", job->message);
      free(errmsg);
      app->live.done = 1;
      app->live.running = 0;
      free(wa);
      return NULL;
    }
    job->owns_temp_audio = 1;
    free(errmsg);
  } else {
    snprintf(job->prepared_audio, sizeof job->prepared_audio, "%s", job->input_path);
    job->owns_temp_audio = 0;
  }

  if (app->live.cancel_requested) {
    job->status = DMX_JOB_CANCELLED;
    snprintf(job->message, sizeof job->message, "Cancelled");
    cleanup_temp(job);
    app->live.done = 1;
    app->live.running = 0;
    free(wa);
    return NULL;
  }

  job->status = DMX_JOB_RUNNING;
  snprintf(job->message, sizeof job->message, "Running...");
  dmx_worker_run_job(&app->worker_cfg, job, &app->live);

  if (app->live.cancel_requested) {
    job->status = DMX_JOB_CANCELLED;
    snprintf(job->message, sizeof job->message, "Cancelled");
  } else if (app->live.exit_code == 0) {
    job->status = DMX_JOB_DONE;
    job->progress_pct = 100.0f;
    snprintf(job->message, sizeof job->message, "Wrote stems to %s/%s", job->out_dir, job->track_name);
  } else {
    job->status = DMX_JOB_ERROR;
    snprintf(job->message, sizeof job->message, "%s",
             app->live.error_msg[0] ? app->live.error_msg : "Worker failed");
  }

  cleanup_temp(job);
  free(wa);
  return NULL;
}

void dmx_app_tick(DmxApp *app) {
  dmx_app_apply_settings(app);

  /* Sync live progress into active job */
  if (app->queue.active_index >= 0 && app->queue.active_index < app->queue.count) {
    DmxJob *j = &app->queue.jobs[app->queue.active_index];
    if (j->status == DMX_JOB_RUNNING || j->status == DMX_JOB_PREPARING) {
      j->progress_pct = app->live.progress_pct;
      if (app->live.status_msg[0]) {
        snprintf(j->message, sizeof j->message, "%s", app->live.status_msg);
      }
    }
  }

  /* Join finished worker */
  if (app->worker_thread && app->live.done && !app->live.running) {
    dmx_thread_join(app->worker_thread);
    app->worker_thread = NULL;
    app->queue.active_index = -1;
    app->cancel_pressed = 0;
    dmx_worker_live_reset(&app->live);
  }

  /* Start next job only when user pressed Start and output folder is set. */
  if (!app->worker_thread) {
    if (!app->processing_enabled) {
      return;
    }
    if (!app->output_dir_accepted || !app->output_dir[0]) {
      return;
    }
    int idx = -1;
    DmxJob *next = dmx_queue_next_queued(&app->queue, &idx);
    if (next) {
      DmxWorkerArgs *wa = (DmxWorkerArgs *)calloc(1, sizeof *wa);
      if (!wa) {
        return;
      }
      wa->app = app;
      wa->job_index = idx;
      app->queue.active_index = idx;
      dmx_worker_live_reset(&app->live);
      if (dmx_thread_spawn(&app->worker_thread, worker_thread_main, wa) != 0) {
        free(wa);
        next->status = DMX_JOB_ERROR;
        snprintf(next->message, sizeof next->message, "Failed to spawn worker thread");
        app->queue.active_index = -1;
        app->worker_thread = NULL;
      }
    }
  }
}
