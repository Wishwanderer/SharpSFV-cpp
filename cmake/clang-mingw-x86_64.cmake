# SharpSFV (C++ port) - CMake toolchain: Clang + LLD targeting 64-bit Windows (MinGW-w64).
# Copyright (C) 2025 L33T (original C# SharpSFV). Modified 2026-10-04: added for the lean
# build's smallest programs. Licensed under GPL-3.0 (../LICENSE.txt).
#
# Needs clang, lld and the MinGW-w64 headers and import libraries (on Debian/Ubuntu: clang, lld,
# mingw-w64; the GCC cross compiler package also provides libgcc). Resources go through windres.
#   cmake -S lean -B build-lean-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/clang-mingw-x86_64.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
