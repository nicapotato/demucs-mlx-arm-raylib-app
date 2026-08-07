#include "app_state.h"
#include "ui.h"
#include "util/resource_dir.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef DMX_APP_VERSION
#define DMX_APP_VERSION "0.0.0"
#endif

static int parse_cli(int argc, char **argv, DmxApp *app, int *out_file_args, int *out_force_headless) {
  int file_args = 0;
  int force_headless = 0;

  /* Pass 1: options (so --out is accepted before file adds). */
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      dmx_app_set_output_dir(app, argv[++i]);
      continue;
    }
    if (strcmp(argv[i], "--wav") == 0) {
      app->write_mp3 = 0;
      continue;
    }
    if (strcmp(argv[i], "--mp3") == 0) {
      app->write_mp3 = 1;
      continue;
    }
    if (strcmp(argv[i], "--headless") == 0) {
      force_headless = 1;
      continue;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      return -1;
    }
    /* File path — handled in pass 2. */
  }

  /* Pass 2: input files. */
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      ++i;
      continue;
    }
    if (strcmp(argv[i], "--wav") == 0 || strcmp(argv[i], "--mp3") == 0 ||
        strcmp(argv[i], "--headless") == 0) {
      continue;
    }
    if (argv[i][0] == '-') {
      continue;
    }
    dmx_app_add_path(app, argv[i]);
    file_args = 1;
  }

  *out_file_args = file_args;
  *out_force_headless = force_headless;
  return 0;
}

static int jobs_pending_or_failed(const DmxApp *app, int *out_failed) {
  int pending = 0;
  int failed = 0;
  for (int i = 0; i < app->queue.count; ++i) {
    DmxJobStatus st = app->queue.jobs[i].status;
    if (st == DMX_JOB_QUEUED || st == DMX_JOB_RUNNING || st == DMX_JOB_PREPARING) {
      pending = 1;
    }
    if (st == DMX_JOB_ERROR || st == DMX_JOB_CANCELLED) {
      failed = 1;
    }
  }
  if (out_failed) {
    *out_failed = failed;
  }
  return pending;
}

/* Process queue without InitWindow / OpenGL (GitHub Actions macOS has no GPU display). */
static int run_headless(DmxApp *app) {
  if (!app->output_dir_accepted || !app->output_dir[0]) {
    fprintf(stderr, "ERROR: --out DIR is required (no default stem output folder)\n");
    return 2;
  }
  if (app->queue.count == 0) {
    fprintf(stderr, "ERROR: --headless requires one or more input files\n");
    return 2;
  }
  if (app->worker_cfg.python_or_worker[0] == '\0') {
    fprintf(stderr, "ERROR: %s\n",
            app->status_line[0] ? app->status_line : "no demucs worker resolved");
    return 2;
  }

  /* Headless has no Start button - process the queue immediately. */
  app->processing_enabled = 1;

  printf("headless: %d job(s) -> %s (worker %s)\n", app->queue.count, app->output_dir,
         app->worker_cfg.python_or_worker);
  fflush(stdout);

  char last_status[DMX_MSG_MAX];
  last_status[0] = '\0';

  for (;;) {
    dmx_app_tick(app);

    if (app->queue.active_index >= 0 && app->queue.active_index < app->queue.count) {
      const DmxJob *j = &app->queue.jobs[app->queue.active_index];
      if (j->message[0] && strcmp(j->message, last_status) != 0) {
        snprintf(last_status, sizeof last_status, "%s", j->message);
        printf("[%s] %.0f%% %s\n", j->track_name, (double)j->progress_pct, j->message);
        fflush(stdout);
      }
    }

    if (!app->worker_thread && app->queue.active_index < 0) {
      int failed = 0;
      if (!jobs_pending_or_failed(app, &failed)) {
        for (int i = 0; i < app->queue.count; ++i) {
          const DmxJob *j = &app->queue.jobs[i];
          printf("done: %s -> %s\n", j->input_path, j->message);
        }
        return failed ? 1 : 0;
      }
    }

    usleep(50 * 1000);
  }
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
      printf("DemucsMLX %s\n", DMX_APP_VERSION);
      return 0;
    }
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("DemucsMLX %s\n", DMX_APP_VERSION);
      printf("Usage: DemucsMLX [options] [files...]\n");
      printf("  --out DIR     Stem output directory (required for headless / file args)\n");
      printf("  --mp3         Write MP3 stems (default)\n");
      printf("  --wav         Write WAV stems\n");
      printf("  --headless    No GUI (CI / machines without GPU display)\n");
      printf("  --version     Print version and exit\n");
      printf("\nWith input files, runs headless by default (no InitWindow).\n");
      printf("GUI: add files anytime; pick Output folder, then press Start (no default path).\n");
      return 0;
    }
  }

  DmxApp app;
  dmx_app_init(&app);

  int file_args = 0;
  int force_headless = 0;
  if (parse_cli(argc, argv, &app, &file_args, &force_headless) != 0) {
    dmx_app_shutdown(&app);
    return 2;
  }
  dmx_app_apply_settings(&app);

  /* File args (or explicit --headless) => no GLFW/OpenGL window. */
  if (file_args || force_headless) {
    int rc = run_headless(&app);
    dmx_app_shutdown(&app);
    return rc;
  }

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(980, 720, "DemucsMLX");
  SetTargetFPS(60);

  if (!SearchAndSetResourceDir("resources")) {
    TraceLog(LOG_WARNING, "resources/ not found; logo/icon may be missing");
  }

  dmx_app_load_branding(&app);
  if (app.output_dir_accepted) {
    snprintf(app.status_line, sizeof app.status_line,
             "Output ready - add files and press Start (v%s)", DMX_APP_VERSION);
  } else {
    snprintf(app.status_line, sizeof app.status_line,
             "Add files, pick output folder, press Start (v%s)", DMX_APP_VERSION);
  }

  while (!WindowShouldClose()) {
    dmx_app_tick(&app);
    dmx_ui_poll(&app);

    if (IsFileDropped()) {
      FilePathList dropped = LoadDroppedFiles();
      for (unsigned int i = 0; i < dropped.count; ++i) {
        dmx_app_add_path(&app, dropped.paths[i]);
      }
      UnloadDroppedFiles(dropped);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      Vector2 m = GetMousePosition();
      dmx_ui_handle_click(&app, (int)m.x, (int)m.y, GetScreenWidth(), GetScreenHeight());
    }

    BeginDrawing();
    dmx_ui_draw(&app, GetScreenWidth(), GetScreenHeight());
    EndDrawing();
  }

  dmx_ui_shutdown();
  dmx_app_shutdown(&app);
  CloseWindow();
  return 0;
}
