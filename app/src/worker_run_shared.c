#include "worker_run_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#define dmx_access _access
#else
#include <unistd.h>
#ifdef __APPLE__
#include <stdint.h>
#endif
#define dmx_access access
#endif

void dmx_worker_config_defaults(DmxWorkerConfig *cfg) {
  memset(cfg, 0, sizeof *cfg);
  snprintf(cfg->model_name, sizeof cfg->model_name, "htdemucs_6s");
  cfg->write_mp3 = 1;
  cfg->batch_size = 8;
}

void dmx_worker_live_reset(DmxWorkerLive *live) {
  memset(live, 0, sizeof *live);
  live->exit_code = -1;
}

void dmx_worker_set_msg(char *buf, size_t n, const char *s) {
  if (!buf || n == 0) {
    return;
  }
  snprintf(buf, n, "%s", s ? s : "");
}

int dmx_worker_file_exists(const char *p) {
  return p && p[0] && dmx_access(p, 0) == 0;
}

void dmx_worker_dirname_copy(const char *path, char *out, size_t out_sz) {
  snprintf(out, out_sz, "%s", path);
  char *slash = strrchr(out, '/');
#ifdef _WIN32
  char *bslash = strrchr(out, '\\');
  if (!slash || (bslash && bslash > slash)) {
    slash = bslash;
  }
#endif
  if (slash) {
    *slash = '\0';
  } else {
    snprintf(out, out_sz, ".");
  }
}

int dmx_worker_exe_path(char *out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return -1;
  }
  out[0] = '\0';
#ifdef _WIN32
  wchar_t wexe[DMX_PATH_MAX];
  DWORD n = GetModuleFileNameW(NULL, wexe, (DWORD)(sizeof wexe / sizeof wexe[0]));
  if (n == 0 || n >= (DWORD)(sizeof wexe / sizeof wexe[0])) {
    return -1;
  }
  int need = WideCharToMultiByte(CP_UTF8, 0, wexe, -1, out, (int)out_sz, NULL, NULL);
  return need > 0 ? 0 : -1;
#elif defined(__APPLE__)
  uint32_t sz = (uint32_t)out_sz;
  extern int _NSGetExecutablePath(char *buf, uint32_t *bufsize);
  if (_NSGetExecutablePath(out, &sz) != 0) {
    out[0] = '\0';
    return -1;
  }
  return 0;
#else
  ssize_t n = readlink("/proc/self/exe", out, out_sz - 1);
  if (n <= 0) {
    out[0] = '\0';
    return -1;
  }
  out[n] = '\0';
  return 0;
#endif
}

static int try_path(DmxWorkerConfig *cfg, const char *path, const char *module) {
  if (!dmx_worker_file_exists(path)) {
    return 0;
  }
  snprintf(cfg->python_or_worker, sizeof cfg->python_or_worker, "%s", path);
  if (module && module[0]) {
    snprintf(cfg->module_or_empty, sizeof cfg->module_or_empty, "%s", module);
  } else {
    cfg->module_or_empty[0] = '\0';
  }
  return 1;
}

int dmx_worker_resolve(DmxWorkerConfig *cfg, char *err, size_t err_sz) {
  if (!cfg) {
    return -1;
  }

#ifdef _WIN32
  const char *env_torch_worker = getenv("DEMUCS_TORCH_WORKER");
  const char *env_torch_python = getenv("DEMUCS_TORCH_PYTHON");
#endif
  const char *env_mlx_worker = getenv("DEMUCS_MLX_WORKER");
  const char *env_mlx_python = getenv("DEMUCS_MLX_PYTHON");

#ifdef _WIN32
  if (try_path(cfg, env_torch_worker, NULL)) {
    /* frozen torch worker */
  } else if (try_path(cfg, env_mlx_worker, NULL)) {
    /* frozen mlx worker (unexpected on Windows) */
  } else if (try_path(cfg, env_torch_python, "demucs_torch.separate")) {
    /* dev python -m */
  } else if (try_path(cfg, env_mlx_python, "demucs_mlx.separate")) {
    /* fallback */
  } else {
    char exe[DMX_PATH_MAX];
    char exe_dir[DMX_PATH_MAX];
    char candidate[DMX_PATH_MAX];
    if (dmx_worker_exe_path(exe, sizeof exe) != 0) {
      exe[0] = '\0';
    }
    dmx_worker_dirname_copy(exe, exe_dir, sizeof exe_dir);

    snprintf(candidate, sizeof candidate, "%s\\worker\\demucs_torch_worker.exe", exe_dir);
    if (!try_path(cfg, candidate, NULL)) {
      snprintf(candidate, sizeof candidate, "%s\\demucs_torch_worker.exe", exe_dir);
      try_path(cfg, candidate, NULL);
    }
    if (!dmx_worker_file_exists(cfg->python_or_worker)) {
      snprintf(candidate, sizeof candidate, "%s\\..\\..\\..\\.venv\\Scripts\\python.exe", exe_dir);
      if (!try_path(cfg, candidate, "demucs_torch.separate")) {
        snprintf(candidate, sizeof candidate, "%s\\..\\..\\.venv\\Scripts\\python.exe", exe_dir);
        if (!try_path(cfg, candidate, "demucs_torch.separate")) {
          snprintf(err, err_sz, "No demucs worker found. Set DEMUCS_TORCH_PYTHON or DEMUCS_TORCH_WORKER.");
          return -1;
        }
      }
    }
  }
#else
  if (try_path(cfg, env_mlx_worker, NULL)) {
    /* frozen mlx worker */
  } else if (try_path(cfg, env_mlx_python, "demucs_mlx.separate")) {
    /* dev python -m */
  } else {
    char exe[DMX_PATH_MAX];
    char exe_dir[DMX_PATH_MAX];
    char candidate[DMX_PATH_MAX];
    if (dmx_worker_exe_path(exe, sizeof exe) != 0) {
      exe[0] = '\0';
    }
    dmx_worker_dirname_copy(exe, exe_dir, sizeof exe_dir);

    snprintf(candidate, sizeof candidate, "%s/../Resources/worker/demucs_mlx_worker", exe_dir);
    if (!try_path(cfg, candidate, NULL)) {
      snprintf(candidate, sizeof candidate, "%s/worker/demucs_mlx_worker", exe_dir);
      if (!try_path(cfg, candidate, NULL)) {
        snprintf(candidate, sizeof candidate, "%s/demucs_mlx_worker", exe_dir);
        try_path(cfg, candidate, NULL);
      }
    }
    if (!dmx_worker_file_exists(cfg->python_or_worker)) {
      snprintf(candidate, sizeof candidate, "%s/../../.venv/bin/python", exe_dir);
      if (!try_path(cfg, candidate, "demucs_mlx.separate")) {
        snprintf(candidate, sizeof candidate, "%s/../../../.venv/bin/python", exe_dir);
        if (!try_path(cfg, candidate, "demucs_mlx.separate")) {
          snprintf(err, err_sz, "No demucs worker found. Set DEMUCS_MLX_PYTHON or DEMUCS_MLX_WORKER.");
          return -1;
        }
      }
    }
  }
#endif

  const char *env_torch_cache = getenv("DEMUCS_TORCH_CACHE");
  const char *env_mlx_cache = getenv("DEMUCS_MLX_CACHE");
  if (env_torch_cache && env_torch_cache[0]) {
    snprintf(cfg->model_cache_dir, sizeof cfg->model_cache_dir, "%s", env_torch_cache);
  } else if (env_mlx_cache && env_mlx_cache[0]) {
    snprintf(cfg->model_cache_dir, sizeof cfg->model_cache_dir, "%s", env_mlx_cache);
  } else {
    char exe[DMX_PATH_MAX];
    char exe_dir[DMX_PATH_MAX];
    char candidate[DMX_PATH_MAX];
    if (dmx_worker_exe_path(exe, sizeof exe) != 0) {
      exe[0] = '\0';
    }
    dmx_worker_dirname_copy(exe, exe_dir, sizeof exe_dir);
#ifdef _WIN32
    snprintf(candidate, sizeof candidate, "%s\\models", exe_dir);
#else
    snprintf(candidate, sizeof candidate, "%s/../Resources/models", exe_dir);
    if (!dmx_worker_file_exists(candidate)) {
      snprintf(candidate, sizeof candidate, "%s/models", exe_dir);
    }
#endif
    if (dmx_worker_file_exists(candidate)) {
      snprintf(cfg->model_cache_dir, sizeof cfg->model_cache_dir, "%s", candidate);
    } else {
#ifdef _WIN32
      const char *appdata = getenv("LOCALAPPDATA");
      if (appdata && appdata[0]) {
        snprintf(cfg->model_cache_dir, sizeof cfg->model_cache_dir, "%s\\demucs-mlx-app\\models",
                 appdata);
      }
#else
      const char *home = getenv("HOME");
      if (home) {
        snprintf(cfg->model_cache_dir, sizeof cfg->model_cache_dir, "%s/.cache/demucs-mlx", home);
      }
#endif
    }
  }

  return 0;
}

static void copy_json_string(const char *line, const char *key, char *out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return;
  }
  out[0] = '\0';
  const char *p = strstr(line, key);
  if (!p) {
    return;
  }
  p = strchr(p, ':');
  if (!p) {
    return;
  }
  p++;
  while (*p == ' ' || *p == '"') {
    p++;
  }
  size_t i = 0;
  while (*p && *p != '"' && *p != ',' && *p != '}' && i + 1 < out_sz) {
    out[i++] = *p++;
  }
  out[i] = '\0';
}

void dmx_worker_handle_json_line(DmxWorkerLive *live, const char *line) {
  const char *ev = strstr(line, "\"event\"");
  if (!ev) {
    return;
  }
  if (strstr(line, "\"separating\"")) {
    const char *pct = strstr(line, "\"pct\"");
    if (pct) {
      pct = strchr(pct, ':');
      if (pct) {
        live->progress_pct = (float)atof(pct + 1);
      }
    }
    dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Separating...");
  } else if (strstr(line, "\"loading\"")) {
    dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Loading audio...");
  } else if (strstr(line, "\"writing\"")) {
    dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Writing stems...");
  } else if (strstr(line, "\"done\"")) {
    live->progress_pct = 100.0f;
    dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Done");
  } else if (strstr(line, "\"error\"")) {
    const char *msg = strstr(line, "\"message\"");
    if (msg) {
      msg = strchr(msg, ':');
      if (msg) {
        msg++;
        while (*msg == ' ' || *msg == '"') {
          msg++;
        }
        char tmp[DMX_MSG_MAX];
        size_t i = 0;
        while (*msg && *msg != '"' && i + 1 < sizeof tmp) {
          tmp[i++] = *msg++;
        }
        tmp[i] = '\0';
        dmx_worker_set_msg(live->error_msg, sizeof live->error_msg, tmp);
      }
    }
  } else if (strstr(line, "\"status\"")) {
    char device[32];
    copy_json_string(line, "\"device\"", device, sizeof device);
    if (device[0]) {
      snprintf(live->device, sizeof live->device, "%s", device);
    }
    if (strstr(line, "loading_model")) {
      if (live->device[0]) {
        snprintf(live->status_msg, sizeof live->status_msg, "Loading model (%s)...", live->device);
      } else {
        dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Loading model...");
      }
    } else if (strstr(line, "model_ready")) {
      if (strcmp(live->device, "cpu") == 0) {
        dmx_worker_set_msg(live->status_msg, sizeof live->status_msg,
                           "Model ready (CPU — a 4-minute track may take about a minute)");
      } else if (live->device[0]) {
        snprintf(live->status_msg, sizeof live->status_msg, "Model ready (%s)", live->device);
      } else {
        dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Model ready");
      }
    }
  }
}
