// OpenCLLoader.cpp - the OpenCL entry points Leela uses, forwarded to the system's
// OpenCL library (OpenCL.dll / libOpenCL.so.1 / OpenCL.framework), which is loaded
// on first use. Nothing links against OpenCL, so the program builds without an SDK.
#include <CL/cl.h>

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
void * library() {
    static void * handle = [] {
#ifdef _WIN32
        void * h = reinterpret_cast<void *>(LoadLibraryA("OpenCL.dll"));
#elif defined(__APPLE__)
        void * h = dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW);
#else
        void * h = dlopen("libOpenCL.so.1", RTLD_NOW);
        if (!h) h = dlopen("libOpenCL.so", RTLD_NOW);
#endif
        return h;
    }();
    return handle;
}

void * symbol(const char * name) {
    void * h = library();
    if (!h) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(h), name));
#else
    return dlsym(h, name);
#endif
}
}

// No OpenCL library (or it lacks the function): the calls fail as if no platform
// were installed (CL_PLATFORM_NOT_FOUND_KHR).
constexpr cl_int CL_LIBRARY_MISSING = -1001;

#include "OpenCLFunctions.inc"
