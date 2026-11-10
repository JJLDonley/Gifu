#include "base/thread.h"

#include <stdlib.h>

#if defined(OS_WINDOWS)
#include <windows.h>

_Static_assert(sizeof(SRWLOCK) <= sizeof(((Mutex *)0)->storage), "grow Mutex storage");

typedef struct ThreadStart {
    ThreadMain main;
    void *arg;
} ThreadStart;

static DWORD WINAPI thread_trampoline(LPVOID param) {
    ThreadStart start = *(ThreadStart *)param;
    free(param);
    return (DWORD)start.main(start.arg);
}

bool thread_start(Thread *thread, ThreadMain main, void *arg) {
    ThreadStart *start = malloc(sizeof(ThreadStart));
    if (!start) return false;
    *start = (ThreadStart){ main, arg };
    HANDLE h = CreateThread(NULL, 0, thread_trampoline, start, 0, NULL);
    if (!h) {
        free(start);
        return false;
    }
    thread->handle = h;
    return true;
}

void thread_join(Thread *thread) {
    if (!thread->handle) return;
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    thread->handle = NULL;
}
void thread_detach(Thread *thread) {
    if (!thread->handle) return;
    CloseHandle(thread->handle);   // the thread keeps running and cleans up after itself
    thread->handle = NULL;
}

void mutex_init(Mutex *m) { InitializeSRWLock((SRWLOCK *)m->storage); }
void mutex_lock(Mutex *m) { AcquireSRWLockExclusive((SRWLOCK *)m->storage); }
void mutex_unlock(Mutex *m) { ReleaseSRWLockExclusive((SRWLOCK *)m->storage); }
void mutex_destroy(Mutex *m) { UNUSED(m); }

void run_once(OnceFlag *flag, void (*fn)(void)) {
    if (flag->state == 2) return;
    if (InterlockedCompareExchange((volatile LONG *)&flag->state, 1, 0) == 0) {
        fn();
        InterlockedExchange((volatile LONG *)&flag->state, 2);
        return;
    }
    while (flag->state != 2) Sleep(0);
}

#else
#include <pthread.h>

_Static_assert(sizeof(pthread_mutex_t) <= sizeof(((Mutex *)0)->storage), "grow Mutex storage");

typedef struct ThreadStart {
    ThreadMain main;
    void *arg;
} ThreadStart;

static void *thread_trampoline(void *param) {
    ThreadStart start = *(ThreadStart *)param;
    free(param);
    start.main(start.arg);
    return NULL;
}

bool thread_start(Thread *thread, ThreadMain main, void *arg) {
    ThreadStart *start = malloc(sizeof(ThreadStart));
    if (!start) return false;
    *start = (ThreadStart){ main, arg };
    pthread_t *t = malloc(sizeof(pthread_t));
    if (!t || pthread_create(t, NULL, thread_trampoline, start) != 0) {
        free(start);
        free(t);
        return false;
    }
    thread->handle = t;
    return true;
}

void thread_join(Thread *thread) {
    if (!thread->handle) return;
    pthread_join(*(pthread_t *)thread->handle, NULL);
    free(thread->handle);
    thread->handle = NULL;
}
void thread_detach(Thread *thread) {
    if (!thread->handle) return;
    pthread_detach(*(pthread_t *)thread->handle);
    free(thread->handle);
    thread->handle = NULL;
}

void mutex_init(Mutex *m) { pthread_mutex_init((pthread_mutex_t *)m->storage, NULL); }
void mutex_lock(Mutex *m) { pthread_mutex_lock((pthread_mutex_t *)m->storage); }
void mutex_unlock(Mutex *m) { pthread_mutex_unlock((pthread_mutex_t *)m->storage); }
void mutex_destroy(Mutex *m) { pthread_mutex_destroy((pthread_mutex_t *)m->storage); }

static pthread_mutex_t g_once_lock = PTHREAD_MUTEX_INITIALIZER;

void run_once(OnceFlag *flag, void (*fn)(void)) {
    if (__atomic_load_n(&flag->state, __ATOMIC_ACQUIRE) == 2) return;
    pthread_mutex_lock(&g_once_lock);
    if (flag->state != 2) {
        fn();
        __atomic_store_n(&flag->state, 2, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_once_lock);
}
#endif
