// thread.h - minimal threads / mutex / once, portable across glibc, mingw-w64
// (MSYS2, llvm-mingw) and macOS. Win32 primitives on Windows, pthreads elsewhere.
#pragma once

#include "base/base.h"

typedef i32 (*ThreadMain)(void *arg);

typedef struct Thread {
    void *handle;
} Thread;

typedef struct Mutex {
    _Alignas(8) u8 storage[64];   // SRWLOCK or pthread_mutex_t
} Mutex;

typedef struct OnceFlag {
    volatile i32 state;   // 0 = not run, 1 = running, 2 = done
} OnceFlag;

#define ONCE_FLAG_INITIALIZER { 0 }

NODISCARD bool thread_start(Thread *thread, ThreadMain main, void *arg);
void thread_join(Thread *thread);
void thread_detach(Thread *thread);   // let it finish on its own (no join)

void mutex_init(Mutex *mutex);
void mutex_lock(Mutex *mutex);
void mutex_unlock(Mutex *mutex);
void mutex_destroy(Mutex *mutex);

void run_once(OnceFlag *flag, void (*fn)(void));
