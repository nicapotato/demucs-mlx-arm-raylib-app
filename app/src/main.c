#include "app_state.h"
#include "ui.h"
#include "util/resource_dir.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>

#ifndef DMX_APP_VERSION
#define DMX_APP_VERSION "0.0.0"
#endif

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
      printf("demucs mlx app %s\n", DMX_APP_VERSION);
      return 0;
    }
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("demucs mlx app %s\n", DMX_APP_VERSION);
      printf("Usage: demucs-mlx-app [options] [files...]\n");
      printf("  --out DIR     Stem output directory\n");
      printf("  --mp3         Write MP3 stems (default)\n");
      printf("  --wav         Write WAV stems\n");
      printf("  --version     Print version and exit\n");
      return 0;
    }
  }

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(980, 720, "demucs mlx app");
  SetTargetFPS(60);

  if (!SearchAndSetResourceDir("resources")) {
    TraceLog(LOG_WARNING, "resources/ not found; logo/icon may be missing");
  }

  DmxApp app;
  dmx_app_init(&app);
  dmx_app_load_branding(&app);
  snprintf(app.status_line, sizeof app.status_line, "Ready - drop audio or PSARC files (v%s)",
           DMX_APP_VERSION);

  /* CLI paths for headless-ish verification: demucs_mlx_app file1 file2 --out DIR */
  int file_args = 0;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      snprintf(app.output_dir, sizeof app.output_dir, "%s", argv[++i]);
      continue;
    }
    if (strcmp(argv[i], "--wav") == 0) {
      app.write_mp3 = 0;
      continue;
    }
    if (strcmp(argv[i], "--mp3") == 0) {
      app.write_mp3 = 1;
      continue;
    }
    if (argv[i][0] == '-') {
      continue;
    }
    dmx_app_add_path(&app, argv[i]);
    file_args = 1;
  }
  dmx_app_apply_settings(&app);

  int auto_quit_when_idle = file_args;
  int saw_work = app.queue.count > 0;

  while (!WindowShouldClose()) {
    dmx_app_tick(&app);

    if (IsFileDropped()) {
      FilePathList dropped = LoadDroppedFiles();
      for (unsigned int i = 0; i < dropped.count; ++i) {
        dmx_app_add_path(&app, dropped.paths[i]);
        saw_work = 1;
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

    if (auto_quit_when_idle && saw_work && app.queue.count > 0 && !app.worker_thread &&
        app.queue.active_index < 0) {
      int pending = 0;
      int failed = 0;
      for (int i = 0; i < app.queue.count; ++i) {
        if (app.queue.jobs[i].status == DMX_JOB_QUEUED || app.queue.jobs[i].status == DMX_JOB_RUNNING ||
            app.queue.jobs[i].status == DMX_JOB_PREPARING) {
          pending = 1;
        }
        if (app.queue.jobs[i].status == DMX_JOB_ERROR) {
          failed = 1;
        }
      }
      if (!pending) {
        dmx_app_shutdown(&app);
        CloseWindow();
        return failed ? 1 : 0;
      }
    }
  }

  dmx_app_shutdown(&app);
  CloseWindow();
  return 0;
}
