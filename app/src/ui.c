#include "ui.h"

#include "platform/threads.h"
#include "tinyfiledialogs.h"

#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strtok_r strtok_s
#endif

#ifndef DMX_APP_VERSION
#define DMX_APP_VERSION "0.0.0"
#endif

#define DMX_DLG_RESULT_MAX 8192

typedef enum {
  DMX_DLG_NONE = 0,
  DMX_DLG_FILES,
  DMX_DLG_FOLDER,
} DmxDlgKind;

typedef struct {
  DmxDlgKind kind;
  DmxThread *thread;
  volatile int done;
  char start_dir[DMX_PATH_MAX];
  char result[DMX_DLG_RESULT_MAX];
} DmxDlgState;

typedef struct {
  float controls_y;
  float info_y;
  float drop_y;
  Rectangle add_btn;
  Rectangle out_btn;
  Rectangle model_btn;
  Rectangle fmt_btn;
  Rectangle start_stop_btn;
  Rectangle path_box;
  Rectangle drop;
} DmxUiLayout;

static DmxDlgState g_dlg;

static int point_in(int x, int y, Rectangle r) {
  return x >= (int)r.x && x < (int)(r.x + r.width) && y >= (int)r.y && y < (int)(r.y + r.height);
}

static Rectangle btn(float x, float y, float w, float h) {
  return (Rectangle){x, y, w, h};
}

static unsigned char clamp_u8(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 255) {
    return 255;
  }
  return (unsigned char)v;
}

static Color brighten(Color c, int delta) {
  return (Color){clamp_u8((int)c.r + delta), clamp_u8((int)c.g + delta), clamp_u8((int)c.b + delta), c.a};
}

static Color dim_color(Color c) {
  return (Color){(unsigned char)(c.r / 2), (unsigned char)(c.g / 2), (unsigned char)(c.b / 2), c.a};
}

static int dlg_busy(void) {
  return g_dlg.kind != DMX_DLG_NONE && g_dlg.thread != NULL && !g_dlg.done;
}

/* enabled=0: dimmed, no hover. Returns 1 if mouse is over an enabled button. */
static int draw_button(Rectangle r, const char *label, Color bg, int enabled) {
  Vector2 m = GetMousePosition();
  int hover = enabled && point_in((int)m.x, (int)m.y, r);
  int pressed = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

  Color fill = bg;
  Color border = Fade(WHITE, 0.35f);
  Color text = WHITE;
  float border_w = 1.0f;
  int label_dy = 0;

  if (!enabled) {
    fill = dim_color(bg);
    text = (Color){140, 140, 148, 255};
    border = Fade(WHITE, 0.15f);
  } else if (pressed) {
    fill = brighten(bg, -28);
    border = Fade(WHITE, 0.65f);
    border_w = 2.0f;
    label_dy = 1;
  } else if (hover) {
    fill = brighten(bg, 28);
    border = Fade(WHITE, 0.55f);
    border_w = 2.0f;
  }

  DrawRectangleRec(r, fill);
  DrawRectangleLinesEx(r, border_w, border);
  int tw = MeasureText(label, 18);
  DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2) + label_dy, 18, text);
  return hover;
}

/* Single layout for draw + hit-testing (must stay in sync). */
static DmxUiLayout dmx_ui_layout(const DmxApp *app, int screen_w) {
  DmxUiLayout L;
  memset(&L, 0, sizeof L);

  float header_bottom = 72.0f;
  if (app->logo_loaded && app->logo_tex.id > 0) {
    const float draw_h = 40.0f;
    header_bottom = 16.0f + draw_h + 6.0f + 20.0f;
  }

  L.controls_y = header_bottom + 12.0f;
  if (L.controls_y < 90.0f) {
    L.controls_y = 90.0f;
  }

  L.add_btn = btn(24, L.controls_y, 140, 36);
  L.out_btn = btn(176, L.controls_y, 160, 36);
  L.model_btn = btn(348, L.controls_y, 200, 36);
  L.fmt_btn = btn(560, L.controls_y, 120, 36);
  L.start_stop_btn = btn((float)screen_w - 140.0f, L.controls_y, 116, 36);

  L.info_y = L.controls_y + 48.0f;
  L.path_box = (Rectangle){20, L.info_y - 4.0f, (float)screen_w - 40.0f, 28.0f};

  L.drop_y = L.info_y + 78.0f;
  L.drop = (Rectangle){24, L.drop_y, (float)screen_w - 48, 80};
  return L;
}

static void strip_trailing_crlf(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
    s[--n] = '\0';
  }
}

static void *dlg_files_thread(void *arg) {
  DmxDlgState *d = (DmxDlgState *)arg;
  const char *filters[] = {"*.mp3", "*.wav", "*.ogg", "*.flac", "*.m4a", "*.psarc"};
  const char *path = tinyfd_openFileDialog("Add audio or PSARC", d->start_dir, 6, filters, "Audio / PSARC", 1);
  d->result[0] = '\0';
  if (path && path[0]) {
    snprintf(d->result, sizeof d->result, "%s", path);
  }
  d->done = 1;
  return NULL;
}

static void *dlg_folder_thread(void *arg) {
  DmxDlgState *d = (DmxDlgState *)arg;
  const char *start = d->start_dir[0] ? d->start_dir : NULL;
  const char *dir = tinyfd_selectFolderDialog("Stem output folder", start);
  d->result[0] = '\0';
  if (dir && dir[0]) {
    snprintf(d->result, sizeof d->result, "%s", dir);
    strip_trailing_crlf(d->result);
  }
  d->done = 1;
  return NULL;
}

static int begin_dialog(DmxApp *app, DmxDlgKind kind) {
  if (dlg_busy()) {
    snprintf(app->status_line, sizeof app->status_line, "A file dialog is already open");
    return 0;
  }
  if (g_dlg.thread) {
    dmx_thread_join(g_dlg.thread);
    g_dlg.thread = NULL;
  }

  memset(&g_dlg, 0, sizeof g_dlg);
  g_dlg.kind = kind;
  g_dlg.done = 0;
  g_dlg.result[0] = '\0';

  if (kind == DMX_DLG_FOLDER) {
    if (app->output_dir[0]) {
      snprintf(g_dlg.start_dir, sizeof g_dlg.start_dir, "%s", app->output_dir);
    } else {
      const char *home = getenv("HOME");
      if (home && home[0]) {
        snprintf(g_dlg.start_dir, sizeof g_dlg.start_dir, "%s", home);
      }
    }
  }

  void *(*fn)(void *) = (kind == DMX_DLG_FILES) ? dlg_files_thread : dlg_folder_thread;
  if (dmx_thread_spawn(&g_dlg.thread, fn, &g_dlg) != 0) {
    g_dlg.kind = DMX_DLG_NONE;
    g_dlg.thread = NULL;
    snprintf(app->status_line, sizeof app->status_line, "Could not open file dialog");
    return 0;
  }

  /* Keep a normal arrow over the GLFW window while Finder is open. */
  SetMouseCursor(MOUSE_CURSOR_DEFAULT);
  snprintf(app->status_line, sizeof app->status_line,
           kind == DMX_DLG_FILES ? "Choose files in the dialog..." : "Choose output folder in the dialog...");
  return 1;
}

void dmx_ui_poll(DmxApp *app) {
  if (!app || g_dlg.kind == DMX_DLG_NONE || !g_dlg.thread || !g_dlg.done) {
    return;
  }

  DmxDlgKind kind = g_dlg.kind;
  /* Join first so result writes from the dialog thread are fully visible. */
  dmx_thread_join(g_dlg.thread);
  g_dlg.thread = NULL;

  char result[DMX_DLG_RESULT_MAX];
  snprintf(result, sizeof result, "%s", g_dlg.result);
  g_dlg.kind = DMX_DLG_NONE;
  g_dlg.done = 0;
  g_dlg.result[0] = '\0';

  if (kind == DMX_DLG_FILES) {
    if (!result[0]) {
      snprintf(app->status_line, sizeof app->status_line, "No files selected");
      return;
    }
    char *save = NULL;
    char *tok = strtok_r(result, "|", &save);
    while (tok) {
      dmx_app_add_path(app, tok);
      tok = strtok_r(NULL, "|", &save);
    }
    return;
  }

  if (kind == DMX_DLG_FOLDER) {
    if (result[0]) {
      dmx_app_set_output_dir(app, result);
    } else {
      snprintf(app->status_line, sizeof app->status_line, "Output folder not selected");
    }
  }
}

void dmx_ui_shutdown(void) {
  if (g_dlg.thread) {
    /* Dialog may still be open; join waits until user dismisses it. */
    dmx_thread_join(g_dlg.thread);
    g_dlg.thread = NULL;
  }
  memset(&g_dlg, 0, sizeof g_dlg);
}

void dmx_ui_draw(DmxApp *app, int screen_w, int screen_h) {
  ClearBackground((Color){18, 20, 24, 255});

  if (app->logo_loaded && app->logo_tex.id > 0) {
    const float target_h = 40.0f;
    float scale = target_h / (float)app->logo_tex.height;
    float draw_w = (float)app->logo_tex.width * scale;
    float draw_h = target_h;
    Rectangle src = {0, 0, (float)app->logo_tex.width, (float)app->logo_tex.height};
    Rectangle dst = {24, 16, draw_w, draw_h};
    DrawTexturePro(app->logo_tex, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
#ifdef _WIN32
    const char *subtitle = "Windows stem separator (Demucs + CUDA)";
#else
    const char *subtitle = "Apple Silicon stem separator";
#endif
    DrawText(subtitle, 24, (int)(16 + draw_h + 6), 16, (Color){160, 160, 168, 255});
  } else {
    DrawText("DemucsMLX", 24, 20, 32, (Color){240, 236, 228, 255});
#ifdef _WIN32
    DrawText("Windows stem separator (Demucs + CUDA)", 24, 56, 16, (Color){160, 160, 168, 255});
#else
    DrawText("Apple Silicon stem separator", 24, 56, 16, (Color){160, 160, 168, 255});
#endif
  }

  DmxUiLayout L = dmx_ui_layout(app, screen_w);

  const int out_ok = app->output_dir_accepted && app->output_dir[0];
  const Color out_border = out_ok ? (Color){70, 180, 100, 255} : (Color){210, 70, 70, 255};
  const int queued = dmx_app_queued_count(app);
  const int start_ready = out_ok && queued > 0;
  const int busy = dlg_busy();

  int any_hover = 0;
  any_hover |= draw_button(L.add_btn, "Add files...", (Color){52, 96, 140, 255}, !busy);
  any_hover |= draw_button(L.out_btn, "Output folder...", (Color){52, 96, 140, 255}, !busy);
  DrawRectangleLinesEx(
      (Rectangle){L.out_btn.x - 2, L.out_btn.y - 2, L.out_btn.width + 4, L.out_btn.height + 4}, 2.0f,
      out_border);
  any_hover |= draw_button(L.model_btn, app->model_index == 0 ? "Model: htdemucs_6s" : "Model: htdemucs",
                           (Color){70, 70, 80, 255}, 1);
  any_hover |= draw_button(L.fmt_btn, app->write_mp3 ? "Out: MP3" : "Out: WAV", (Color){70, 70, 80, 255}, 1);

  if (app->processing_enabled) {
    any_hover |= draw_button(L.start_stop_btn, "Stop", (Color){180, 55, 55, 255}, 1);
  } else {
    Color start_bg = (Color){50, 140, 80, 255};
    if (!start_ready) {
      start_bg = dim_color(start_bg);
    }
    any_hover |= draw_button(L.start_stop_btn, "Start", start_bg, 1);
  }

  if (busy) {
    /* Main loop is alive; force a normal arrow (no beach-ball wait cursor). */
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
  } else {
    Vector2 m = GetMousePosition();
    if (point_in((int)m.x, (int)m.y, L.path_box)) {
      any_hover = 1;
    }
    SetMouseCursor(any_hover ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
  }

  DrawRectangleRec(L.path_box, (Color){24, 26, 32, 255});
  DrawRectangleLinesEx(L.path_box, 2.0f, out_border);
  if (out_ok) {
    DrawText(TextFormat("Output: %s", app->output_dir), 28, (int)L.info_y, 16,
             (Color){180, 180, 188, 255});
  } else {
    DrawText("Output: (not set - pick a folder)", 28, (int)L.info_y, 16, (Color){210, 120, 120, 255});
  }
  DrawText(TextFormat("Worker: %s", app->worker_cfg.python_or_worker), 24, (int)L.info_y + 30, 14,
           (Color){120, 120, 128, 255});
  if (app->live.device[0]) {
    const char *dev = app->live.device;
    Color dev_c = (strcmp(dev, "cpu") == 0) ? (Color){210, 140, 70, 255} : (Color){120, 120, 128, 255};
    if (strcmp(dev, "cpu") == 0) {
      DrawText("Device: CPU (slow — NVIDIA CUDA recommended)", 24, (int)L.info_y + 48, 14, dev_c);
    } else {
      DrawText(TextFormat("Device: %s", dev), 24, (int)L.info_y + 48, 14, dev_c);
    }
  } else if (app->worker_cfg.model_cache_dir[0]) {
    DrawText(TextFormat("Models: %s", app->worker_cfg.model_cache_dir), 24, (int)L.info_y + 48, 14,
             (Color){120, 120, 128, 255});
  }

  DrawRectangleRec(L.drop, (Color){28, 32, 40, 255});
  DrawRectangleLinesEx(L.drop, 2.0f, (Color){80, 90, 110, 255});
#ifdef _WIN32
  const char *busy_hint = "File dialog open - finish or cancel in the picker";
#else
  const char *busy_hint = "File dialog open - finish or cancel in Finder";
#endif
  const char *hint = busy ? busy_hint
                          : (out_ok ? "Drop MP3 / WAV / OGG / FLAC / PSARC (_p or _m) here - then press Start"
                                    : "Drop files here - pick output folder and press Start");
  int tw = MeasureText(hint, 18);
  DrawText(hint, (int)(L.drop.x + (L.drop.width - tw) / 2), (int)(L.drop.y + 30), 18,
           (Color){150, 155, 170, 255});

  int y = (int)(L.drop_y + 100.0f);
  DrawText(TextFormat("Jobs (%d queued)", queued), 24, y - 24, 20, (Color){220, 220, 220, 255});
  for (int i = 0; i < app->queue.count; ++i) {
    DmxJob *j = &app->queue.jobs[i];
    Rectangle row = {24, (float)y, (float)screen_w - 48, 52};
    Color bg = (i == app->queue.active_index) ? (Color){36, 44, 58, 255} : (Color){26, 28, 34, 255};
    DrawRectangleRec(row, bg);
    DrawText(j->display_name, 36, y + 8, 18, WHITE);
    DrawText(TextFormat("%s  %.0f%%  %s", dmx_job_status_label(j->status), j->progress_pct, j->message), 36,
             y + 30, 14, (Color){170, 170, 178, 255});

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
  DmxUiLayout L = dmx_ui_layout(app, screen_w);

  if (point_in(x, y, L.add_btn)) {
    if (dlg_busy()) {
      return 1;
    }
    begin_dialog(app, DMX_DLG_FILES);
    return 1;
  }
  if (point_in(x, y, L.out_btn) || point_in(x, y, L.path_box)) {
    if (dlg_busy()) {
      return 1;
    }
    begin_dialog(app, DMX_DLG_FOLDER);
    return 1;
  }
  if (point_in(x, y, L.model_btn)) {
    app->model_index = 1 - app->model_index;
    dmx_app_apply_settings(app);
    return 1;
  }
  if (point_in(x, y, L.fmt_btn)) {
    app->write_mp3 = !app->write_mp3;
    dmx_app_apply_settings(app);
    return 1;
  }
  if (point_in(x, y, L.start_stop_btn)) {
    if (app->processing_enabled) {
      dmx_app_stop(app);
    } else {
      dmx_app_start(app);
    }
    return 1;
  }
  return 0;
}
