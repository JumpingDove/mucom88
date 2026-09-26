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

option(MUCOM88_ENABLE_ASAN_UBSAN
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MUCOM88_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(MUCOM88_ENABLE_ASAN_UBSAN AND MUCOM88_ENABLE_TSAN)
    message(FATAL_ERROR
        "MUCOM88_ENABLE_ASAN_UBSAN and MUCOM88_ENABLE_TSAN are mutually exclusive")
endif()
if(MUCOM88_ENABLE_ASAN_UBSAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
elseif(MUCOM88_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

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
    editor/mml_document.cpp
    editor/service_types.cpp
    editor/serial_executor.cpp
    editor/application_services.cpp
    editor/document_service.cpp
    editor/editor_command.cpp
    editor/editor_line_model.cpp
    editor/recovery_service.cpp
    editor/mucom_compile_service.cpp
    editor/audio_device_service.cpp
    editor/playback_session.cpp
    editor/playback_coordinator.cpp
    editor/export_service.cpp
    editor/voice_service.cpp
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

if(APPLE)
    option(MUCOM88_BUILD_MACOS_EDITOR "Build the native macOS MML editor" ON)
    option(MUCOM88_ADHOC_SIGN_EDITOR
        "Apply an ad-hoc signature to the development editor bundle" ON)
else()
    set(MUCOM88_BUILD_MACOS_EDITOR OFF)
endif()

if(APPLE AND MUCOM88_BUILD_MACOS_EDITOR)
    enable_language(OBJCXX)

    add_executable(mucom88_editor MACOSX_BUNDLE
        editor/macos/mucom_editor.mm
    )
    target_compile_options(mucom88_editor PRIVATE -fobjc-arc)
    target_link_libraries(mucom88_editor PRIVATE
        mucom88_runtime
        "-framework AppKit"
    )
    set_target_properties(mucom88_editor PROPERTIES
        OUTPUT_NAME "MUCOM88Editor"
        MACOSX_BUNDLE_BUNDLE_NAME "MUCOM88 Editor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.mucom88.editor"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/macos/Info.plist.in"
    )

    if(MUCOM88_ADHOC_SIGN_EDITOR)
        add_custom_command(TARGET mucom88_editor POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign -
                "$<TARGET_BUNDLE_DIR:mucom88_editor>"
            COMMENT "Applying ad-hoc signature to MUCOM88Editor.app"
            VERBATIM
        )
    endif()

    install(TARGETS mucom88_editor BUNDLE DESTINATION .)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
