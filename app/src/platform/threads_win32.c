#define WIN32_LEAN_AND_MEAN
#include "platform/threads.h"

#include <windows.h>
#include <stdlib.h>

struct DmxThread {
  HANDLE h;
};

typedef struct {
  void *(*fn)(void *);
  void *arg;
} dmx_thread_boot;

static DWORD WINAPI dmx_thread_trampoline(LPVOID p) {
  dmx_thread_boot *b = (dmx_thread_boot *)p;
  void *(*fn)(void *) = b->fn;
  void *arg = b->arg;
  free(b);
  fn(arg);
  return 0;
}

int dmx_thread_spawn(DmxThread **out, void *(*start_routine)(void *), void *arg) {
  DmxThread *t = (DmxThread *)calloc(1, sizeof *t);
  if (!t) {
    return -1;
  }
  dmx_thread_boot *b = (dmx_thread_boot *)malloc(sizeof *b);
  if (!b) {
    free(t);
    return -1;
  }
  b->fn = start_routine;
  b->arg = arg;
  t->h = CreateThread(NULL, 0, dmx_thread_trampoline, b, 0, NULL);
  if (!t->h) {
    free(b);
    free(t);
    return -1;
  }
  *out = t;
  return 0;
}

void dmx_thread_join(DmxThread *t) {
  if (!t) {
    return;
  }
  WaitForSingleObject(t->h, INFINITE);
  CloseHandle(t->h);
  free(t);
}
