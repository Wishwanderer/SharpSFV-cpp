# SharpSFV (C++ port)
# Copyright (C) 2025 L33T (original C# SharpSFV)
# Modified 2026-10-02: ported from C# (.NET 10) to C++20. Licensed under GPL-3.0 (LICENSE.txt).
# Modified 2026-10-03: GUI target; size-optimised release builds.
# Modified 2026-10-04: the resources are rebuilt when the icon or manifest changes.
cmake_minimum_required(VERSION 3.20)
project(SharpSFV VERSION 2.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SHARPSFV_BUILD_TESTS "Build the unit tests" ON)
option(SHARPSFV_STATIC_RUNTIME "Link the C/C++ runtime statically (single self-contained .exe)" ON)
option(SHARPSFV_GUI_EMBED_HEADLESS "Build the command-line mode into SharpSFV.exe too (otherwise -headless runs SharpSFV-cli.exe)" OFF)
option(SHARPSFV_OPTIMIZE_SIZE "Release builds: optimise for size, except the hashing code (kept at full speed)" ON)

# Size: the program is mostly UI and bookkeeping, which size optimisation shrinks a lot at no
# visible cost. Hasher.cpp (CRC-32, xxHash3: the hot loop) keeps speed optimisation. RTTI is
# not used (exceptions do not need it).
if(SHARPSFV_OPTIMIZE_SIZE)
  if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/O1> $<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Gw> /GR-)
    add_link_options($<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    set_source_files_properties(src/core/Hasher.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:/O2>")
  else()
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-Oz SHARPSFV_HAS_OZ) # GCC 12+, Clang
    if(SHARPSFV_HAS_OZ)
      set(_sfv_size_opt -Oz)
    else()
      set(_sfv_size_opt -Os)
    endif()
    add_compile_options($<$<CONFIG:Release>:${_sfv_size_opt}> $<$<CONFIG:Release>:-ffunction-sections>
                        $<$<CONFIG:Release>:-fdata-sections> $<$<CONFIG:Release>:-fno-ident>
                        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    if(NOT APPLE)
      add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections> $<$<CONFIG:Release>:-s>)
    endif()
    set_source_files_properties(src/core/Hasher.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-O2>")
  endif()
endif()

# ---------------------------------------------------------------- core library
add_library(sharpsfv_core STATIC
  src/core/AppSettings.cpp
  src/core/ChecksumParser.cpp
  src/core/ChecksumWriter.cpp
  src/core/Console.cpp
  src/core/DataStorage.cpp
  src/core/DriveClassifier.cpp
  src/core/DriveDetector.cpp
  src/core/Engine.cpp
  src/core/EnvPath.cpp
  src/core/FileFilter.cpp
  src/core/FileStore.cpp
  src/core/FileSystem.cpp
  src/core/Hasher.cpp
  src/core/Headless.cpp
  src/core/JobStore.cpp
  src/core/ListModel.cpp
  src/core/PathUtil.cpp
  src/core/Scripts.cpp
  src/core/SortUtil.cpp
  src/core/StringPool.cpp
  src/core/Threading.cpp
  src/core/Text.cpp
  src/core/Types.cpp
)
target_include_directories(sharpsfv_core PUBLIC src/core PRIVATE third_party/xxhash)

# Size (GCC/Clang release builds): exception support is kept only where exceptions are thrown or
# caught (Engine, FileStore, FileSystem, Threading, the GUI's main.cpp). Elsewhere a C++
# exception can only mean the process is out of memory, and it still propagates to the same
# handler; the tables for running destructors on the way are what -fno-exceptions removes.
set(SHARPSFV_NO_EH_SOURCES
  src/core/AppSettings.cpp src/core/ChecksumParser.cpp src/core/ChecksumWriter.cpp src/core/Console.cpp
  src/core/DataStorage.cpp src/core/DriveClassifier.cpp src/core/DriveDetector.cpp src/core/EnvPath.cpp
  src/core/FileFilter.cpp src/core/Headless.cpp src/core/JobStore.cpp src/core/ListModel.cpp
  src/core/PathUtil.cpp src/core/Scripts.cpp src/core/SortUtil.cpp src/core/StringPool.cpp src/core/Text.cpp src/core/Types.cpp
  src/gui/Dialogs.cpp src/gui/MainWindow.cpp src/gui/MainWindowActions.cpp src/gui/MainWindowList.cpp
  src/gui/MainWindowMenus.cpp src/gui/MainWindowRun.cpp src/gui/Panel.cpp src/gui/SingleInstance.cpp
  src/gui/SystemIntegration.cpp src/gui/Win32Util.cpp)
if(SHARPSFV_OPTIMIZE_SIZE AND NOT MSVC)
  set_source_files_properties(${SHARPSFV_NO_EH_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-fno-exceptions>")
endif()

find_package(Threads REQUIRED)
target_link_libraries(sharpsfv_core PUBLIC Threads::Threads)

if(WIN32)
  target_compile_definitions(sharpsfv_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_link_libraries(sharpsfv_core PUBLIC bcrypt advapi32 shell32 ole32 uuid user32)
else()
  # Non-Windows builds (portable CLI and tests) use OpenSSL for MD5/SHA-1/SHA-256.
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  target_link_libraries(sharpsfv_core PUBLIC OpenSSL::Crypto)
endif()

if(MSVC)
  target_compile_options(sharpsfv_core PUBLIC /utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
  if(SHARPSFV_STATIC_RUNTIME)
    set_property(TARGET sharpsfv_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
else()
  target_compile_options(sharpsfv_core PRIVATE -Wall -Wextra)
endif()

function(sharpsfv_executable_settings tgt)
  if(MSVC AND SHARPSFV_STATIC_RUNTIME)
    set_property(TARGET ${tgt} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  if(MINGW)
    if(ARGN MATCHES "UNICODE_ENTRY")
      target_link_options(${tgt} PRIVATE -municode) # wmain / wWinMain
    endif()
    if(SHARPSFV_STATIC_RUNTIME)
      target_link_options(${tgt} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    if(ARGN MATCHES "SHIPPED")
      target_sources(${tgt} PRIVATE src/shared/RuntimeTrim.cpp) # see the file: trims libstdc++ (MinGW)
    endif()
  endif()
endfunction()

# ---------------------------------------------------------------- command-line tool
add_executable(SharpSFV-cli src/cli/main.cpp)
target_link_libraries(SharpSFV-cli PRIVATE sharpsfv_core)
sharpsfv_executable_settings(SharpSFV-cli UNICODE_ENTRY SHIPPED)

# ---------------------------------------------------------------- GUI (Windows)
if(WIN32)
  enable_language(RC)
  add_executable(SharpSFV WIN32
    src/gui/main.cpp
    src/gui/Dialogs.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindowActions.cpp
    src/gui/MainWindowList.cpp
    src/gui/MainWindowMenus.cpp
    src/gui/MainWindowRun.cpp
    src/gui/Panel.cpp
    src/gui/SingleInstance.cpp
    src/gui/SystemIntegration.cpp
    src/gui/Win32Util.cpp
    res/SharpSFV.rc
  )
  target_include_directories(SharpSFV PRIVATE src/gui res)
  set_source_files_properties(res/SharpSFV.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/res/SharpSFV.ico;${CMAKE_CURRENT_SOURCE_DIR}/res/SharpSFV.manifest")
  target_link_libraries(SharpSFV PRIVATE sharpsfv_core comctl32 comdlg32 shell32 ole32 uuid user32 gdi32)
  sharpsfv_executable_settings(SharpSFV UNICODE_ENTRY SHIPPED)
  if(SHARPSFV_GUI_EMBED_HEADLESS)
    target_compile_definitions(SharpSFV PRIVATE SHARPSFV_GUI_EMBED_HEADLESS)
  endif()
  if(NOT MSVC)
    target_compile_options(SharpSFV PRIVATE -Wall -Wextra)
  endif()
  if(MSVC)
    target_link_options(SharpSFV PRIVATE /MANIFEST:NO) # the .rc embeds res/SharpSFV.manifest
  endif()
endif()

# ---------------------------------------------------------------- tests
if(SHARPSFV_BUILD_TESTS)
  enable_testing()
  add_executable(sharpsfv_tests tests/test_main.cpp)
  target_link_libraries(sharpsfv_tests PRIVATE sharpsfv_core)
  sharpsfv_executable_settings(sharpsfv_tests)
  add_test(NAME unit COMMAND sharpsfv_tests)

  # Probe used by scripts/compare_with_oracle.sh (diffs the port against the C# logic).
  add_executable(sharpsfv_probe tests/probe_main.cpp)
  target_link_libraries(sharpsfv_probe PRIVATE sharpsfv_core)
  sharpsfv_executable_settings(sharpsfv_probe)
endif()
