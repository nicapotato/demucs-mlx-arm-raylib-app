#ifndef DMX_WORKER_RUN_INTERNAL_H
#define DMX_WORKER_RUN_INTERNAL_H

#include "worker_run.h"

void dmx_worker_set_msg(char *buf, size_t n, const char *s);
void dmx_worker_handle_json_line(DmxWorkerLive *live, const char *line);
int dmx_worker_file_exists(const char *p);
void dmx_worker_dirname_copy(const char *path, char *out, size_t out_sz);
int dmx_worker_exe_path(char *out, size_t out_sz);

#endif
