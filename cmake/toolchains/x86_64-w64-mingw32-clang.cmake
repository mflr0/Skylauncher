# Windows x64 cross-compilation with llvm-mingw (clang, lld, libc++, UCRT): https://github.com/mstorsjo/llvm-mingw
# Tested with release 20260922 on macOS:
#
#   cmake -S . -B build-win64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32-clang.cmake \
#         -DLLVM_MINGW_ROOT=<extracted llvm-mingw release> -DCMAKE_BUILD_TYPE=Release
#
# Without LLVM_MINGW_ROOT the x86_64-w64-mingw32-* tools are taken from PATH.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(LLVM_MINGW_ROOT "${LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw installation directory")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT)

if(LLVM_MINGW_ROOT)
    set(SKYLAUNCHER_MINGW_PREFIX "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32")
    set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32")
else()
    set(SKYLAUNCHER_MINGW_PREFIX "x86_64-w64-mingw32")
endif()

set(CMAKE_C_COMPILER "${SKYLAUNCHER_MINGW_PREFIX}-clang")
set(CMAKE_CXX_COMPILER "${SKYLAUNCHER_MINGW_PREFIX}-clang++")
set(CMAKE_RC_COMPILER "${SKYLAUNCHER_MINGW_PREFIX}-windres")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# A single executable: libc++, libunwind and winpthreads are linked in; only system DLLs and the UCRT remain.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
