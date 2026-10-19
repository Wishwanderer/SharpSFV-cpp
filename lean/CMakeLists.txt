# SharpSFV (lean C++ rewrite, without the C++ standard library)
# Copyright (C) 2025 L33T (original C# SharpSFV)
# Modified 2026-10-03: added for the rewrite. Licensed under GPL-3.0 (../LICENSE.txt).
# Modified 2026-10-04: threads, drive detection and DriveType.ini sources.
# Modified 2026-10-04: the GUI (SharpSFV.exe) and the GUI's core pieces.
# Modified 2026-10-04: size options (Clang + LLD: link-time optimization, identical code folding).
# Modified 2026-10-04: SFV_LEAN_OPT; resources rebuilt when the icon or manifest changes.
# Modified 2026-10-06: SharpSFV Lite (SharpSFV-Lite.exe, verify-only, for installers) and its tests.
#
# Windows, smallest programs (Clang + LLD, cross or native; see lean/README.md):
#   cmake -S lean -B build-lean-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/clang-mingw-x86_64.cmake
# Windows with MinGW-w64 GCC (works too; somewhat larger programs):
#   cmake -S lean -B build-lean-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-x86_64.cmake
# Linux (the test build: same engine code on POSIX, with sanitizers available):
#   cmake -S lean -B build-lean -G Ninja [-DSFV_LEAN_SANITIZE=ON]
# (A relative toolchain path is looked up next to lean/, hence ../cmake/.)
cmake_minimum_required(VERSION 3.20)
project(SharpSFVLean VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
option(SFV_LEAN_SANITIZE "Linux test build: AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
# Size options for the two shipped Windows programs (MinGW: GCC or Clang).
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(SFV_LTO_DEFAULT ON)
else()
  set(SFV_LTO_DEFAULT OFF) # GCC 13 for MinGW stops with an internal error under -flto
endif()
option(SFV_LEAN_LTO "Link-time optimization of the shipped programs (Clang + LLD)" ${SFV_LTO_DEFAULT})
option(SFV_LEAN_NO_UNWIND "Leave out x64 unwind tables (smaller; breaks stack walking: crash reports lose their call stacks)" OFF)
set(SFV_HASH_OPT "-O2" CACHE STRING "Optimization of core/Hash.cpp, the hashing loops (the rest is optimized for size)")
set(SFV_LEAN_OPT "-Oz" CACHE STRING "Optimization of everything else in the shipped programs")

set(SFV_LEAN_SOURCES
  base/Base.cpp base/FileSystem.cpp base/Path.cpp base/System.cpp base/Sort.cpp base/Text.cpp base/Thread.cpp
  core/ChecksumFile.cpp core/Cli.cpp core/Engine.cpp core/Filter.cpp core/Hash.cpp core/Settings.cpp
  core/Store.cpp core/Types.cpp core/Drive.cpp core/DriveStore.cpp)
# Used by the window only (the console program does not link them).
set(SFV_GUI_CORE_SOURCES core/JobRunner.cpp core/JobStore.cpp core/ListModel.cpp core/Scripts.cpp)
# SharpSFV Lite (verify-only, for installers): its run loop and OS services.
set(SFV_LITE_SOURCES lite/LiteVerify.cpp lite/LitePlatform.cpp)
# What SharpSFV-Lite.exe links from the shared code (the GNU linker cannot drop unused
# functions, so listing only these keeps the GCC build small too).
set(SFV_LITE_SHARED_SOURCES base/Base.cpp base/FileSystem.cpp base/Path.cpp base/System.cpp base/Text.cpp
  base/Thread.cpp core/ChecksumFile.cpp core/Drive.cpp core/Hash.cpp core/Store.cpp core/Types.cpp)
set(SFV_XXHASH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/xxhash)

if(WIN32)
  # ---------------------------------------------------------------- shipped Windows build
  # No C++ runtime, no C runtime start-up code: the program links only Windows DLLs
  # (kernel32, bcrypt) and msvcrt.dll for memcpy/malloc (used by xxHash). See base/Base.h.
  # Settings shared by SharpSFV-cli.exe and SharpSFV.exe.
  function(sfv_shipped target entry)
    target_include_directories(${target} PRIVATE ${SFV_XXHASH_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../res)
    target_compile_definitions(${target} PRIVATE SFV_FREESTANDING UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                                                 _WIN32_WINNT=0x0A00)
    target_compile_options(${target} PRIVATE ${SFV_LEAN_OPT} -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-ident
                           -ffunction-sections -fdata-sections -fno-stack-protector -Wall -Wextra)
    target_link_options(${target} PRIVATE -nostdlib -nostartfiles -Wl,-e,${entry} -Wl,--gc-sections -s)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      # LLD: fold functions with identical code; its --gc-sections also drops unused functions
      # together with their unwind entries (GNU ld keeps every function that has one).
      target_link_options(${target} PRIVATE -fuse-ld=lld -Wl,--icf=all)
    endif()
    if(SFV_LEAN_LTO)
      target_compile_options(${target} PRIVATE -flto)
      target_link_options(${target} PRIVATE -flto)
    endif()
    if(SFV_LEAN_NO_UNWIND)
      target_compile_options(${target} PRIVATE -fno-asynchronous-unwind-tables -fno-unwind-tables)
    endif()
  endfunction()

  if(MSVC)
    message(FATAL_ERROR "The lean build needs MinGW-w64 (GCC or Clang); see lean/README.md")
  endif()
  set_source_files_properties(core/Hash.cpp PROPERTIES COMPILE_OPTIONS "${SFV_HASH_OPT}") # the hot loop keeps full speed

  add_executable(SharpSFV-cli ${SFV_LEAN_SOURCES} app/CliMain.cpp)
  sfv_shipped(SharpSFV-cli mainCRTStartup)
  target_link_libraries(SharpSFV-cli PRIVATE msvcrt kernel32 bcrypt gcc)

  # The window, SharpSFV.exe: the same freestanding set-up, with the Windows GUI libraries.
  # Its -headless mode runs the command-line code in-process (no SharpSFV-cli.exe needed).
  enable_language(RC)
  set(SFV_GUI_SOURCES gui/GuiMain.cpp gui/Dialogs.cpp gui/MainWindow.cpp gui/MainWindowActions.cpp gui/MainWindowList.cpp
      gui/MainWindowMenus.cpp gui/MainWindowRun.cpp gui/Panel.cpp gui/SingleInstance.cpp gui/SystemIntegration.cpp
      gui/Win32Util.cpp)
  add_executable(SharpSFV WIN32 ${SFV_LEAN_SOURCES} ${SFV_GUI_CORE_SOURCES} ${SFV_GUI_SOURCES} ../res/SharpSFV.rc)
  sfv_shipped(SharpSFV WinMainCRTStartup)
  set_source_files_properties(../res/SharpSFV.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../res/SharpSFV.ico;${CMAKE_CURRENT_SOURCE_DIR}/../res/SharpSFV.manifest")
  target_link_libraries(SharpSFV PRIVATE msvcrt kernel32 user32 gdi32 comctl32 comdlg32 shell32 ole32 advapi32 bcrypt gcc)

  # SharpSFV Lite: the same freestanding set-up as SharpSFV-cli.exe.
  add_executable(SharpSFV-Lite ${SFV_LITE_SHARED_SOURCES} ${SFV_LITE_SOURCES} app/LiteMain.cpp)
  sfv_shipped(SharpSFV-Lite mainCRTStartup)
  target_link_libraries(SharpSFV-Lite PRIVATE msvcrt kernel32 bcrypt gcc)

  # Unit tests and the differential-test probe for the Windows code paths (ordinary
  # C-runtime programs; their size is irrelevant).
  foreach(t sfvlean_tests sfvlean_probe sfvlite_tests sfvlite_host)
    if(t STREQUAL "sfvlean_tests")
      add_executable(${t} tests/LeanTests.cpp ${SFV_LEAN_SOURCES} ${SFV_GUI_CORE_SOURCES})
    elseif(t STREQUAL "sfvlite_tests")
      add_executable(${t} tests/LiteTests.cpp ${SFV_LEAN_SOURCES} ${SFV_LITE_SOURCES})
    elseif(t STREQUAL "sfvlite_host") # stands in for an Inno Setup script (Pause, Stop, status file)
      add_executable(${t} tests/LiteHost.cpp ${SFV_LEAN_SOURCES} ${SFV_LITE_SOURCES})
    else()
      add_executable(${t} tests/LeanProbe.cpp ${SFV_LEAN_SOURCES} ${SFV_GUI_CORE_SOURCES})
    endif()
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${SFV_XXHASH_DIR})
    target_compile_definitions(${t} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(${t} PRIVATE bcrypt)
    if(NOT MSVC)
      target_compile_options(${t} PRIVATE -O2 -fno-exceptions -fno-rtti)
      target_link_options(${t} PRIVATE -static)
    endif()
  endforeach()
else()
  # ---------------------------------------------------------------- Linux test build
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_library(sfvlean STATIC ${SFV_LEAN_SOURCES} ${SFV_GUI_CORE_SOURCES} ${SFV_LITE_SOURCES})
  target_include_directories(sfvlean PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${SFV_XXHASH_DIR})
  target_compile_options(sfvlean PUBLIC -fno-exceptions -fno-rtti PRIVATE -Wall -Wextra)
  find_package(Threads REQUIRED)
  target_link_libraries(sfvlean PUBLIC OpenSSL::Crypto Threads::Threads)
  if(SFV_LEAN_SANITIZE)
    target_compile_options(sfvlean PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all -g)
    target_link_options(sfvlean PUBLIC -fsanitize=address,undefined)
  endif()
  add_executable(sfvlean-cli app/CliMain.cpp)
  target_link_libraries(sfvlean-cli PRIVATE sfvlean)
  add_executable(sfvlean_tests tests/LeanTests.cpp)
  target_link_libraries(sfvlean_tests PRIVATE sfvlean)
  add_executable(sfvlean_probe tests/LeanProbe.cpp)
  target_link_libraries(sfvlean_probe PRIVATE sfvlean)
  add_executable(sfvlite app/LiteMain.cpp)
  target_link_libraries(sfvlite PRIVATE sfvlean)
  add_executable(sfvlite_tests tests/LiteTests.cpp)
  target_link_libraries(sfvlite_tests PRIVATE sfvlean)
endif()
