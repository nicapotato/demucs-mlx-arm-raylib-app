#include "ui.h"

#include "tinyfiledialogs.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>

#ifndef DMX_APP_VERSION
#define DMX_APP_VERSION "0.0.0"
#endif

static int point_in(int x, int y, Rectangle r) {
  return x >= (int)r.x && x < (int)(r.x + r.width) && y >= (int)r.y && y < (int)(r.y + r.height);
}

static Rectangle btn(float x, float y, float w, float h) {
  return (Rectangle){x, y, w, h};
}

static void draw_button(Rectangle r, const char *label, Color bg) {
  DrawRectangleRec(r, bg);
  DrawRectangleLinesEx(r, 1.0f, Fade(WHITE, 0.35f));
  int tw = MeasureText(label, 18);
  DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18, WHITE);
}

void dmx_ui_draw(DmxApp *app, int screen_w, int screen_h) {
  ClearBackground((Color){18, 20, 24, 255});

  float header_bottom = 72.0f;
  if (app->logo_loaded && app->logo_tex.id > 0) {
    const float target_h = 40.0f;
    float scale = target_h / (float)app->logo_tex.height;
    float draw_w = (float)app->logo_tex.width * scale;
    float draw_h = target_h;
    Rectangle src = {0, 0, (float)app->logo_tex.width, (float)app->logo_tex.height};
    Rectangle dst = {24, 16, draw_w, draw_h};
    DrawTexturePro(app->logo_tex, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
    DrawText("Apple Silicon stem separator", 24, (int)(16 + draw_h + 6), 16,
             (Color){160, 160, 168, 255});
    header_bottom = 16 + draw_h + 6 + 20;
  } else {
    DrawText("demucs mlx app", 24, 20, 32, (Color){240, 236, 228, 255});
    DrawText("Apple Silicon stem separator", 24, 56, 16, (Color){160, 160, 168, 255});
  }

  float controls_y = header_bottom + 12.0f;
  if (controls_y < 90.0f) {
    controls_y = 90.0f;
  }

  /* Controls row */
  Rectangle add_btn = btn(24, controls_y, 140, 36);
  Rectangle out_btn = btn(176, controls_y, 160, 36);
  Rectangle model_btn = btn(348, controls_y, 200, 36);
  Rectangle fmt_btn = btn(560, controls_y, 120, 36);
  Rectangle cancel_btn = btn(screen_w - 140.0f, controls_y, 116, 36);

  draw_button(add_btn, "Add files...", (Color){52, 96, 140, 255});
  draw_button(out_btn, "Output folder...", (Color){52, 96, 140, 255});
  draw_button(model_btn, app->model_index == 0 ? "Model: htdemucs_6s" : "Model: htdemucs",
              (Color){70, 70, 80, 255});
  draw_button(fmt_btn, app->write_mp3 ? "Out: MP3" : "Out: WAV", (Color){70, 70, 80, 255});
  draw_button(cancel_btn, "Cancel", (Color){140, 60, 60, 255});

  float info_y = controls_y + 48.0f;
  DrawText(TextFormat("Output: %s", app->output_dir), 24, (int)info_y, 16, (Color){180, 180, 188, 255});
  DrawText(TextFormat("Worker: %s", app->worker_cfg.python_or_worker), 24, (int)info_y + 22, 14,
           (Color){120, 120, 128, 255});
  if (app->worker_cfg.model_cache_dir[0]) {
    DrawText(TextFormat("Models: %s", app->worker_cfg.model_cache_dir), 24, (int)info_y + 40, 14,
             (Color){120, 120, 128, 255});
  }

  /* Drop zone */
  float drop_y = info_y + 70.0f;
  Rectangle drop = {24, drop_y, (float)screen_w - 48, 80};
  DrawRectangleRec(drop, (Color){28, 32, 40, 255});
  DrawRectangleLinesEx(drop, 2.0f, (Color){80, 90, 110, 255});
  const char *hint = "Drop MP3 / WAV / OGG / FLAC / PSARC here";
  int tw = MeasureText(hint, 20);
  DrawText(hint, (int)(drop.x + (drop.width - tw) / 2), (int)(drop.y + 30), 20, (Color){150, 155, 170, 255});

  /* Job list */
  int y = (int)(drop_y + 100.0f);
  DrawText("Jobs", 24, y - 24, 20, (Color){220, 220, 220, 255});
  for (int i = 0; i < app->queue.count; ++i) {
    DmxJob *j = &app->queue.jobs[i];
    Rectangle row = {24, (float)y, (float)screen_w - 48, 52};
    Color bg = (i == app->queue.active_index) ? (Color){36, 44, 58, 255} : (Color){26, 28, 34, 255};
    DrawRectangleRec(row, bg);
    DrawText(j->display_name, 36, y + 8, 18, WHITE);
    DrawText(TextFormat("%s  %.0f%%  %s", dmx_job_status_label(j->status), j->progress_pct, j->message), 36,
             y + 30, 14, (Color){170, 170, 178, 255});

    /* Progress bar */
    float bar_w = row.width - 24;
    DrawRectangle(36, y + 46, (int)bar_w, 4, (Color){40, 40, 48, 255});
    float fill = bar_w * (j->progress_pct / 100.0f);
    if (fill > 0) {
      DrawRectangle(36, y + 46, (int)fill, 4, (Color){90, 170, 120, 255});
    }
    y += 58;
    if (y > screen_h - 60) {
      break;
    }
  }

  DrawText(app->status_line, 24, screen_h - 36, 16, (Color){200, 200, 200, 255});
  const char *ver = TextFormat("v%s", DMX_APP_VERSION);
  int vw = MeasureText(ver, 14);
  DrawText(ver, screen_w - vw - 24, screen_h - 34, 14, (Color){120, 120, 128, 255});
}

int dmx_ui_handle_click(DmxApp *app, int x, int y, int screen_w, int screen_h) {
  (void)screen_h;
  /* Keep hitboxes aligned with dmx_ui_draw layout (logo header ~72px). */
  float controls_y = 90.0f;
  Rectangle add_btn = btn(24, controls_y, 140, 36);
  Rectangle out_btn = btn(176, controls_y, 160, 36);
  Rectangle model_btn = btn(348, controls_y, 200, 36);
  Rectangle fmt_btn = btn(560, controls_y, 120, 36);
  Rectangle cancel_btn = btn(screen_w - 140.0f, controls_y, 116, 36);

  if (point_in(x, y, add_btn)) {
    const char *filters[] = {"*.mp3", "*.wav", "*.ogg", "*.flac", "*.m4a", "*.psarc"};
    const char *path = tinyfd_openFileDialog("Add audio or PSARC", "", 6, filters, "Audio / PSARC", 1);
    if (path && path[0]) {
      /* tinyfd multi-select returns | separated paths */
      char buf[8192];
      snprintf(buf, sizeof buf, "%s", path);
      char *save = NULL;
      char *tok = strtok_r(buf, "|", &save);
      while (tok) {
        dmx_app_add_path(app, tok);
        tok = strtok_r(NULL, "|", &save);
      }
    }
    return 1;
  }
  if (point_in(x, y, out_btn)) {
    const char *dir = tinyfd_selectFolderDialog("Stem output folder", app->output_dir);
    if (dir && dir[0]) {
      snprintf(app->output_dir, sizeof app->output_dir, "%s", dir);
      snprintf(app->status_line, sizeof app->status_line, "Output -> %s", dir);
    }
    return 1;
  }
  if (point_in(x, y, model_btn)) {
    app->model_index = 1 - app->model_index;
    dmx_app_apply_settings(app);
    return 1;
  }
  if (point_in(x, y, fmt_btn)) {
    app->write_mp3 = !app->write_mp3;
    dmx_app_apply_settings(app);
    return 1;
  }
  if (point_in(x, y, cancel_btn)) {
    dmx_app_request_cancel(app);
    return 1;
  }
  return 0;
}
