cmake_minimum_required(VERSION 3.16)
project(flick VERSION 0.1.3 LANGUAGES C)

# the version in the binary: X.Y.Z-dev while developing, X.Y.Z in the release package
# (tools/package_release.sh sets the suffix to empty)
set(FLICK_VERSION_SUFFIX "-dev" CACHE STRING "Appended to the version number")
set(FLICK_VERSION "${PROJECT_VERSION}${FLICK_VERSION_SUFFIX}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)       # unsigned __int128 for exact emulated time
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(HS_SDL "Build the SDL2 frontend (window, input)" ON)

add_executable(flick
  src/main.c src/machine.c src/bus.c src/sched.c src/score7.c src/trace.c
  src/timer.c src/i2c.c src/screen.c src/ppu.c src/png.c src/cdservo.c src/cdimg.c src/input.c src/spu.c src/card.c src/cards.c src/cardlist.c src/font.c src/ui.c src/toolbar.c src/os.c src/dump.c src/config.c src/controls.c)
target_compile_definitions(flick PRIVATE HS_VERSION="${FLICK_VERSION}")
target_compile_options(flick PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-type-limits)
if(WIN32)
  enable_language(RC)
  # icon (res/flick.ico, made by tools/mkicon.py) and version information
  set(FLICK_ICON "${CMAKE_SOURCE_DIR}/res/flick.ico")
  configure_file(res/flick.rc.in "${CMAKE_BINARY_DIR}/flick.rc" @ONLY)
  target_sources(flick PRIVATE "${CMAKE_BINARY_DIR}/flick.rc")
  target_link_libraries(flick PRIVATE comdlg32 shell32)   # file dialogs, UTF-16 command line
else()
  target_compile_definitions(flick PRIVATE _FILE_OFFSET_BITS=64)   # 64-bit fseeko/ftello
  target_link_libraries(flick PRIVATE m)
endif()

if(HS_SDL)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 sdl2)
  endif()
  if(SDL2_FOUND)
    # SDL_MAIN_HANDLED: plain main(), no SDL2main. A GUI program on Windows (no console
    # window on a double click); started from a terminal it prints there (os_console_attach)
    set_target_properties(flick PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(flick PRIVATE HS_HAVE_SDL)
    target_include_directories(flick PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_directories(flick PRIVATE ${SDL2_LIBRARY_DIRS})
    target_link_libraries(flick PRIVATE SDL2)
  else()
    message(STATUS "SDL2 not found: headless-only build")
  endif()
endif()
