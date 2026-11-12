# Cross-compile Windows x64 binaries from Linux/WSL with llvm-mingw
# (clang + mingw-w64 UCRT). Toolchain root: $LLVM_MINGW or ~/.local/share/toolchains/llvm-mingw
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(DEFINED ENV{LLVM_MINGW})
    set(LLVM_MINGW_ROOT $ENV{LLVM_MINGW})
else()
    set(LLVM_MINGW_ROOT $ENV{HOME}/.local/share/toolchains/llvm-mingw)
endif()

set(CMAKE_C_COMPILER ${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER ${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++)   # leela/
set(CMAKE_RC_COMPILER ${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-windres)
set(CMAKE_AR ${LLVM_MINGW_ROOT}/bin/llvm-ar)
set(CMAKE_RANLIB ${LLVM_MINGW_ROOT}/bin/llvm-ranlib)
set(CMAKE_FIND_ROOT_PATH ${LLVM_MINGW_ROOT}/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
