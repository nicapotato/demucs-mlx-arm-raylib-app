#include "worker_run_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
  dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Starting...");

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    dmx_worker_set_msg(live->error_msg, sizeof live->error_msg, "pipe() failed");
    live->running = 0;
    live->done = 1;
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    dmx_worker_set_msg(live->error_msg, sizeof live->error_msg, "fork() failed");
    live->running = 0;
    live->done = 1;
    return -1;
  }

  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    if (cfg->model_cache_dir[0]) {
      setenv("DEMUCS_MLX_CACHE", cfg->model_cache_dir, 1);
      setenv("DEMUCS_TORCH_CACHE", cfg->model_cache_dir, 1);
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

  close(pipefd[1]);
  live->pid = (int)pid;

  FILE *fp = fdopen(pipefd[0], "r");
  char line[2048];
  if (fp) {
    while (fgets(line, sizeof line, fp)) {
      dmx_worker_handle_json_line(live, line);
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
    dmx_worker_set_msg(live->error_msg, sizeof live->error_msg, "waitpid failed");
    live->exit_code = -1;
  } else if (WIFEXITED(status)) {
    live->exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    live->exit_code = 128 + WTERMSIG(status);
    if (live->cancel_requested) {
      dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Cancelled");
    }
  }

  if (live->cancel_requested && live->exit_code != 0) {
    dmx_worker_set_msg(live->status_msg, sizeof live->status_msg, "Cancelled");
  } else if (live->exit_code != 0 && live->error_msg[0] == '\0') {
    snprintf(live->error_msg, sizeof live->error_msg, "Worker exited with code %d", live->exit_code);
  }

  live->pid = 0;
  live->running = 0;
  live->done = 1;
  return live->exit_code == 0 ? 0 : -1;
}
