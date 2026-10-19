# Part of the SharpSFV C++ port (GPL-3.0). Build tooling, added 2026-10-02.
# Modified 2026-10-03: win32 thread model by default (smaller executables).
# Cross-compile for Windows x64 with MinGW-w64, e.g. from Linux:
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
# GCC 13+ supports std::thread with the native "win32" thread model, which avoids linking
# winpthreads (~20 KB per executable). Older toolchains: set SHARPSFV_MINGW_THREADS=posix.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(NOT DEFINED SHARPSFV_MINGW_THREADS)
  set(SHARPSFV_MINGW_THREADS "$ENV{SHARPSFV_MINGW_THREADS}")
endif()
if(NOT SHARPSFV_MINGW_THREADS)
  set(SHARPSFV_MINGW_THREADS win32)
endif()
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-${SHARPSFV_MINGW_THREADS})
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-${SHARPSFV_MINGW_THREADS})
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
