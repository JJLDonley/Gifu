#ifndef UTILS_H_DEFINED
#define UTILS_H_DEFINED


#include "config.h"
#include <string>
#include <atomic>
#include "ThreadPool.h"

#ifdef _MSC_VER
#define ASSUME_ALIGNED(p, n) \
__assume((reinterpret_cast<std::size_t>(p) & ((n) - 1)) == 0)
#else
#define ASSUME_ALIGNED(p, n) \
(p) = static_cast<__typeof__(p)>(__builtin_assume_aligned((p), (n)))
#endif

extern Utils::ThreadPool thread_pool;

namespace Utils {
    void GUIprintf(const char *fmt, ...);
    void GUIAnalysis(void* data);
    void GUIBestMoves(void* data);

    void myprintf(const char *fmt, ...);
    // Small string helpers (replace the Boost ones the original used).
    std::string format(const char *fmt, ...);
    bool starts_with(const std::string & s, const std::string & prefix);
    bool contains(const std::string & s, const std::string & part);
    bool icontains(const std::string & s, const std::string & part);   // ignoring ASCII case
    void trim(std::string & s);
    void gtp_printf(int id, const char *fmt, ...);
    void gtp_fail_printf(int id, const char *fmt, ...);
    void log_input(std::string input);
    bool input_pending();
    bool input_causes_stop();

    template<class T>
    void atomic_add(std::atomic<T> &f, T d) {
        T old = f.load();
        while (!f.compare_exchange_weak(old, old + d));
    }

    template<class T>
    bool is_aligned(T* ptr, size_t alignment) {
        return (uintptr_t(ptr) & (alignment - 1)) == 0;
    }

    template<typename T>
    T rotl(const T x, const int k) {
	    return (x << k) | (x >> (std::numeric_limits<T>::digits - k));
    }
}

#endif
