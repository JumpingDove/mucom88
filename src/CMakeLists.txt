cmake_minimum_required(VERSION 3.20)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
   (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR
    CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL ""))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING
        "Minimum macOS version supported by MUCOM88" FORCE)
endif()

project(mucom88 VERSION 1.7 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(MUCOM88_SDL_TARGET SDL2::SDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(MUCOM88_SDL_TARGET PkgConfig::SDL2)
endif()

set(MUCOM88_CODECONV_SOURCE utils/codeconv/codeconv_dummy.cpp)
if(NOT WIN32)
    find_package(Iconv REQUIRED)
    set(MUCOM88_CODECONV_SOURCE utils/codeconv/codeconv_iconv.cpp)
endif()

add_library(mucom88_runtime STATIC
    membuf.cpp
    adpcm.cpp
    md5.c
    soundbuf.cpp
    cmucom.cpp
    mucomvm.cpp
    mucomerror.cpp
    callback.cpp
    osdep.cpp
    plugin/plugin.cpp
    Z80/Z80.cpp
    fmgen/file.cpp
    fmgen/fmgen.cpp
    fmgen/fmtimer.cpp
    fmgen/opm.cpp
    fmgen/opna.cpp
    fmgen/psg.cpp
    utils/vgmwrite.cpp
    utils/s98write.cpp
    utils/wavwrite.cpp
    ${MUCOM88_CODECONV_SOURCE}
    sdl/osdep_sdl.cpp
    sdl/audiobuffer.cpp
    sdl/audiotime.cpp
)

target_include_directories(mucom88_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(mucom88_runtime PRIVATE USE_SDL=1)
target_link_libraries(mucom88_runtime PRIVATE ${MUCOM88_SDL_TARGET})

if(NOT WIN32)
    target_compile_definitions(mucom88_runtime PRIVATE USE_ICONV=1)
    target_link_libraries(mucom88_runtime PRIVATE Iconv::Iconv)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mucom88_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(mucom88 main.cpp)
target_compile_definitions(mucom88 PRIVATE USE_SDL=1)
target_link_libraries(mucom88 PRIVATE mucom88_runtime ${MUCOM88_SDL_TARGET})

install(TARGETS mucom88 RUNTIME DESTINATION bin)
