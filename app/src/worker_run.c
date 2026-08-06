#include "worker_run.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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

static int file_exists(const char *p) {
  return p && p[0] && access(p, F_OK) == 0;
}

static void dirname_copy(const char *path, char *out, size_t out_sz) {
  snprintf(out, out_sz, "%s", path);
  char *slash = strrchr(out, '/');
  if (slash) {
    *slash = '\0';
  } else {
    snprintf(out, out_sz, ".");
  }
}

int dmx_worker_resolve(DmxWorkerConfig *cfg, char *err, size_t err_sz) {
  if (!cfg) {
    return -1;
  }

  /* 1) Explicit env overrides (dev / packaging) */
  const char *env_worker = getenv("DEMUCS_MLX_WORKER");
  const char *env_python = getenv("DEMUCS_MLX_PYTHON");
  if (env_worker && file_exists(env_worker)) {
    snprintf(cfg->python_or_worker, sizeof cfg->python_or_worker, "%s", env_worker);
    cfg->module_or_empty[0] = '\0';
  } else if (env_python && file_exists(env_python)) {
    snprintf(cfg->python_or_worker, sizeof cfg->python_or_worker, "%s", env_python);
    snprintf(cfg->module_or_empty, sizeof cfg->module_or_empty, "demucs_mlx.separate");
  } else {
    /* 2) Bundled frozen worker next to executable / in .app Resources */
    char exe[PATH_MAX];
    uint32_t sz = sizeof exe;
    exe[0] = '\0';
#if defined(__APPLE__)
    extern int _NSGetExecutablePath(char *buf, uint32_t *bufsize);
    if (_NSGetExecutablePath(exe, &sz) != 0) {
      exe[0] = '\0';
    }
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
      exe[n] = '\0';
    } else {
      exe[0] = '\0';
    }
#endif
    char exe_dir[PATH_MAX];
    dirname_copy(exe, exe_dir, sizeof exe_dir);

    char candidate[PATH_MAX];
    /* .app/Contents/MacOS/../Resources/worker/demucs_mlx_worker */
    snprintf(candidate, sizeof candidate, "%s/../Resources/worker/demucs_mlx_worker", exe_dir);
    if (!file_exists(candidate)) {
      snprintf(candidate, sizeof candidate, "%s/worker/demucs_mlx_worker", exe_dir);
    }
    if (!file_exists(candidate)) {
      snprintf(candidate, sizeof candidate, "%s/demucs_mlx_worker", exe_dir);
    }

    if (file_exists(candidate)) {
      snprintf(cfg->python_or_worker, sizeof cfg->python_or_worker, "%s", candidate);
      cfg->module_or_empty[0] = '\0';
    } else {
      /* 3) Dev: repo .venv python -m demucs_mlx.separate */
      char repo_python[PATH_MAX];
      snprintf(repo_python, sizeof repo_python, "%s/../../.venv/bin/python", exe_dir);
      if (!file_exists(repo_python)) {
        /* build/ is often app/build - go up to repo root */
        snprintf(repo_python, sizeof repo_python, "%s/../../../.venv/bin/python", exe_dir);
      }
      /* Also try absolute known layout from CMAKE binary dir */
      if (!file_exists(repo_python)) {
        const char *env_py = getenv("DEMUCS_MLX_PYTHON");
        if (env_py && file_exists(env_py)) {
          snprintf(repo_python, sizeof repo_python, "%s", env_py);
        }
      }
      if (file_exists(repo_python)) {
        snprintf(cfg->python_or_worker, sizeof cfg->python_or_worker, "%s", repo_python);
        snprintf(cfg->module_or_empty, sizeof cfg->module_or_empty, "demucs_mlx.separate");
      } else {
        snprintf(err, err_sz, "No demucs worker found. Set DEMUCS_MLX_PYTHON or DEMUCS_MLX_WORKER.");
        return -1;
      }
    }
  }

  /* Model cache: env, then bundled models next to binary / Resources */
  const char *env_cache = getenv("DEMUCS_MLX_CACHE");
  if (env_cache && env_cache[0]) {
    snprintf(cfg->model_cache_dir, sizeof cfg->model_cache_dir, "%s", env_cache);
  } else {
    char exe[PATH_MAX];
    uint32_t sz = sizeof exe;
    exe[0] = '\0';
#if defined(__APPLE__)
    extern int _NSGetExecutablePath(char *buf, uint32_t *bufsize);
    if (_NSGetExecutablePath(exe, &sz) != 0) {
      exe[0] = '\0';
    }
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
      exe[n] = '\0';
    }
#endif
    char exe_dir[PATH_MAX];
    dirname_copy(exe, exe_dir, sizeof exe_dir);
    char candidate[PATH_MAX];
    snprintf(candidate, sizeof candidate, "%s/../Resources/models", exe_dir);
    if (!file_exists(candidate)) {
      snprintf(candidate, sizeof candidate, "%s/models", exe_dir);
    }
    if (file_exists(candidate)) {
      snprintf(cfg->model_cache_dir, sizeof cfg->model_cache_dir, "%s", candidate);
    } else {
      const char *home = getenv("HOME");
      if (home) {
        snprintf(cfg->model_cache_dir, sizeof cfg->model_cache_dir, "%s/.cache/demucs-mlx", home);
      }
    }
  }

  return 0;
}

static void set_msg(char *buf, size_t n, const char *s) {
  if (!buf || n == 0) {
    return;
  }
  snprintf(buf, n, "%s", s ? s : "");
}

static void handle_json_line(DmxWorkerLive *live, const char *line) {
  /* Minimal parse: look for "event" and optional pct / message fields. */
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
    set_msg(live->status_msg, sizeof live->status_msg, "Separating...");
  } else if (strstr(line, "\"loading\"")) {
    set_msg(live->status_msg, sizeof live->status_msg, "Loading audio...");
  } else if (strstr(line, "\"writing\"")) {
    set_msg(live->status_msg, sizeof live->status_msg, "Writing stems...");
  } else if (strstr(line, "\"done\"")) {
    live->progress_pct = 100.0f;
    set_msg(live->status_msg, sizeof live->status_msg, "Done");
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
        set_msg(live->error_msg, sizeof live->error_msg, tmp);
      }
    }
  } else if (strstr(line, "\"status\"")) {
    if (strstr(line, "loading_model")) {
      set_msg(live->status_msg, sizeof live->status_msg, "Loading model...");
    } else if (strstr(line, "model_ready")) {
      set_msg(live->status_msg, sizeof live->status_msg, "Model ready");
    }
  }
}

void dmx_worker_request_cancel(DmxWorkerLive *live) {
  if (!live) {
    return;
  }
  live->cancel_requested = 1;
  if (live->pid > 0) {
    kill(live->pid, SIGTERM);
  }
}

int dmx_worker_run_job(const DmxWorkerConfig *cfg, DmxJob *job, DmxWorkerLive *live) {
  if (!cfg || !job || !live) {
    return -1;
  }
  live->running = 1;
  live->done = 0;
  live->progress_pct = 0;
  live->exit_code = -1;
  live->error_msg[0] = '\0';
  set_msg(live->status_msg, sizeof live->status_msg, "Starting...");

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    set_msg(live->error_msg, sizeof live->error_msg, "pipe() failed");
    live->running = 0;
    live->done = 1;
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    set_msg(live->error_msg, sizeof live->error_msg, "fork() failed");
    live->running = 0;
    live->done = 1;
    return -1;
  }

  if (pid == 0) {
    /* Child */
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    /* Keep stderr for debugging; optionally could merge */
    close(pipefd[1]);

    if (cfg->model_cache_dir[0]) {
      setenv("DEMUCS_MLX_CACHE", cfg->model_cache_dir, 1);
    }

    char batch_buf[32];
    snprintf(batch_buf, sizeof batch_buf, "%d", cfg->batch_size > 0 ? cfg->batch_size : 8);

    char *argv[32];
    int a = 0;
    argv[a++] = (char *)cfg->python_or_worker;
    if (cfg->module_or_empty[0]) {
      argv[a++] = "-m";
      argv[a++] = (char *)cfg->module_or_empty;
    }
    argv[a++] = (char *)job->prepared_audio;
    argv[a++] = "-n";
    argv[a++] = (char *)cfg->model_name;
    argv[a++] = "-o";
    argv[a++] = (char *)job->out_dir;
    argv[a++] = "--track-name";
    argv[a++] = (char *)job->track_name;
    argv[a++] = "-b";
    argv[a++] = batch_buf;
    argv[a++] = "--progress-json";
    argv[a++] = "--prefetch-tracks";
    argv[a++] = "0";
    if (cfg->write_mp3) {
      argv[a++] = "--mp3";
    }
    argv[a] = NULL;

    execv(cfg->python_or_worker, argv);
    fprintf(stderr, "execv failed: %s\n", strerror(errno));
    _exit(127);
  }

  /* Parent */
  close(pipefd[1]);
  live->pid = (int)pid;

  FILE *fp = fdopen(pipefd[0], "r");
  char line[2048];
  if (fp) {
    while (fgets(line, sizeof line, fp)) {
      handle_json_line(live, line);
      if (live->cancel_requested) {
        break;
      }
    }
    fclose(fp);
  } else {
    close(pipefd[0]);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    set_msg(live->error_msg, sizeof live->error_msg, "waitpid failed");
    live->exit_code = -1;
  } else if (WIFEXITED(status)) {
    live->exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    live->exit_code = 128 + WTERMSIG(status);
    if (live->cancel_requested) {
      set_msg(live->status_msg, sizeof live->status_msg, "Cancelled");
    }
  }

  if (live->cancel_requested && live->exit_code != 0) {
    set_msg(live->status_msg, sizeof live->status_msg, "Cancelled");
  } else if (live->exit_code != 0 && live->error_msg[0] == '\0') {
    snprintf(live->error_msg, sizeof live->error_msg, "Worker exited with code %d", live->exit_code);
  }

  live->pid = 0;
  live->running = 0;
  live->done = 1;
  return live->exit_code == 0 ? 0 : -1;
}
