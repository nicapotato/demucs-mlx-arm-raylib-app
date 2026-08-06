#include "platform/threads.h"
#include <pthread.h>
#include <stdlib.h>

struct DmxThread {
  pthread_t tid;
};

int dmx_thread_spawn(DmxThread **out, void *(*start_routine)(void *), void *arg) {
  DmxThread *t = (DmxThread *)calloc(1, sizeof *t);
  if (!t) {
    return -1;
  }
  if (pthread_create(&t->tid, NULL, start_routine, arg) != 0) {
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
  pthread_join(t->tid, NULL);
  free(t);
}
