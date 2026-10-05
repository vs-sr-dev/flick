cmake_minimum_required(VERSION 3.16)
project(hyperscanemu C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)       # unsigned __int128 for exact emulated time
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(HS_SDL "Build the SDL2 frontend (window, input)" ON)

add_executable(hyperscanemu
  src/main.c src/machine.c src/bus.c src/sched.c src/score7.c src/trace.c
  src/timer.c src/i2c.c src/screen.c src/ppu.c src/png.c src/cdservo.c src/cdimg.c src/input.c src/spu.c src/card.c src/cards.c src/cardlist.c src/font.c src/ui.c src/toolbar.c src/os.c src/dump.c)
target_compile_options(hyperscanemu PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-type-limits)
if(WIN32)
  target_link_libraries(hyperscanemu PRIVATE comdlg32)   # the "open a disc" dialog
endif()

if(HS_SDL)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 sdl2)
  endif()
  if(SDL2_FOUND)
    # SDL_MAIN_HANDLED: plain console main(), no SDL2main / -mwindows (as Undertow)
    target_compile_definitions(hyperscanemu PRIVATE HS_HAVE_SDL)
    target_include_directories(hyperscanemu PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_directories(hyperscanemu PRIVATE ${SDL2_LIBRARY_DIRS})
    target_link_libraries(hyperscanemu PRIVATE SDL2)
  else()
    message(STATUS "SDL2 not found: headless-only build")
  endif()
endif()
