#include "worker_run_internal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wchar_t *utf8_to_wide(const char *s) {
  if (!s) {
    return NULL;
  }
  int wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (wlen <= 0) {
    return NULL;
  }
  wchar_t *out = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
  if (!out) {
    return NULL;
  }
  if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, wlen) <= 0) {
    free(out);
    return NULL;
  }
  return out;
}

static void append_quoted(char *cmd, size_t cmd_sz, const char *arg) {
  size_t n = strlen(cmd);
  if (n + 4 >= cmd_sz) {
    return;
  }
  if (n > 0) {
    cmd[n++] = ' ';
    cmd[n] = '\0';
  }
  int need_quote = 0;
  for (const char *p = arg; *p; ++p) {
    if (*p == ' ' || *p == '\t' || *p == '"') {
      need_quote = 1;
      break;
    }
  }
  if (!need_quote) {
    snprintf(cmd + n, cmd_sz - n, "%s", arg);
    return;
  }
  if (n + 2 >= cmd_sz) {
    return;
  }
  cmd[n++] = '"';
  cmd[n] = '\0';
  for (const char *p = arg; *p && n + 2 < cmd_sz; ++p) {
    if (*p == '"') {
      cmd[n++] = '\\';
    }
    cmd[n++] = *p;
    cmd[n] = '\0';
  }
  if (n + 1 < cmd_sz) {
    cmd[n++] = '"';
    cmd[n] = '\0';
  }
}

void dmx_worker_request_cancel(DmxWorkerLive *live) {
  if (!live) {
    return;
  }
  live->cancel_requested = 1;
  if (live->os_handle) {
    TerminateProcess((HANDLE)live->os_handle, 1);
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
  live->os_handle = NULL;
  live->error_msg[0] = '\0';
  dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Starting...");

  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof sa);
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE read_pipe = NULL;
  HANDLE write_pipe = NULL;
  if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
    dmx_worker_set_msg(live->error_msg, sizeof live->error_msg, "CreatePipe failed");
    live->running = 0;
    live->done = 1;
    return -1;
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  if (cfg->model_cache_dir[0]) {
    SetEnvironmentVariableA("DEMUCS_TORCH_CACHE", cfg->model_cache_dir);
    SetEnvironmentVariableA("DEMUCS_MLX_CACHE", cfg->model_cache_dir);
    char torch_home[DMX_PATH_MAX];
    snprintf(torch_home, sizeof torch_home, "%s\\torch", cfg->model_cache_dir);
    SetEnvironmentVariableA("TORCH_HOME", torch_home);
  }

  char batch_buf[32];
  snprintf(batch_buf, sizeof batch_buf, "%d", cfg->batch_size > 0 ? cfg->batch_size : 8);

  char cmd[4096];
  cmd[0] = '\0';
  append_quoted(cmd, sizeof cmd, cfg->python_or_worker);
  if (cfg->module_or_empty[0]) {
    append_quoted(cmd, sizeof cmd, "-m");
    append_quoted(cmd, sizeof cmd, cfg->module_or_empty);
  }
  append_quoted(cmd, sizeof cmd, job->prepared_audio);
  append_quoted(cmd, sizeof cmd, "-n");
  append_quoted(cmd, sizeof cmd, cfg->model_name);
  append_quoted(cmd, sizeof cmd, "-o");
  append_quoted(cmd, sizeof cmd, job->out_dir);
  append_quoted(cmd, sizeof cmd, "--track-name");
  append_quoted(cmd, sizeof cmd, job->track_name);
  append_quoted(cmd, sizeof cmd, "-b");
  append_quoted(cmd, sizeof cmd, batch_buf);
  append_quoted(cmd, sizeof cmd, "--progress-json");
  append_quoted(cmd, sizeof cmd, "--prefetch-tracks");
  append_quoted(cmd, sizeof cmd, "0");
  if (cfg->write_mp3) {
    append_quoted(cmd, sizeof cmd, "--mp3");
  }

  wchar_t *wcmd = utf8_to_wide(cmd);
  if (!wcmd) {
    CloseHandle(read_pipe);
    CloseHandle(write_pipe);
    dmx_worker_set_msg(live->error_msg, sizeof live->error_msg, "UTF-16 command line failed");
    live->running = 0;
    live->done = 1;
    return -1;
  }

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = write_pipe;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  free(wcmd);
  CloseHandle(write_pipe);
  if (!ok) {
    CloseHandle(read_pipe);
    dmx_worker_set_msg(live->error_msg, sizeof live->error_msg, "CreateProcessW failed");
    live->running = 0;
    live->done = 1;
    return -1;
  }

  live->os_handle = pi.hProcess;
  live->pid = (int)pi.dwProcessId;
  CloseHandle(pi.hThread);

  char buf[4096];
  char line[2048];
  size_t line_n = 0;
  DWORD nread = 0;
  while (ReadFile(read_pipe, buf, sizeof buf, &nread, NULL) && nread > 0) {
    for (DWORD i = 0; i < nread; ++i) {
      char c = buf[i];
      if (c == '\n' || line_n + 1 >= sizeof line) {
        line[line_n] = '\0';
        if (line_n > 0 && line[line_n - 1] == '\r') {
          line[line_n - 1] = '\0';
        }
        dmx_worker_handle_json_line(live, line);
        line_n = 0;
        if (live->cancel_requested) {
          break;
        }
      } else {
        line[line_n++] = c;
      }
    }
    if (live->cancel_requested) {
      break;
    }
  }
  if (line_n > 0) {
    line[line_n] = '\0';
    dmx_worker_handle_json_line(live, line);
  }
  CloseHandle(read_pipe);

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  live->exit_code = (int)code;
  CloseHandle(pi.hProcess);
  live->os_handle = NULL;
  live->pid = 0;

  if (live->cancel_requested && live->exit_code != 0) {
    dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Cancelled");
  } else if (live->exit_code != 0 && live->error_msg[0] == '\0') {
    snprintf(live->error_msg, sizeof live->error_msg, "Worker exited with code %d", live->exit_code);
  }

  live->running = 0;
  live->done = 1;
  return live->exit_code == 0 ? 0 : -1;
}
