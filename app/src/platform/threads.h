#ifndef DMX_THREADS_H
#define DMX_THREADS_H

typedef struct DmxThread DmxThread;

int dmx_thread_spawn(DmxThread **out, void *(*start_routine)(void *), void *arg);
void dmx_thread_join(DmxThread *t);

#endif
