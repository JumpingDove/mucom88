set(MUCOM88_TEST_WORK_ROOT "${CMAKE_CURRENT_BINARY_DIR}/test-work")
file(MAKE_DIRECTORY "${MUCOM88_TEST_WORK_ROOT}")

function(mucom88_add_test_executable target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE mucom88_runtime ${MUCOM88_SDL_TARGET})
    target_compile_definitions(${target} PRIVATE
        MUCOM88_PROJECT_ROOT="${CMAKE_SOURCE_DIR}/..")
endfunction()

function(mucom88_set_test_properties name timeout)
    set(work_directory "${MUCOM88_TEST_WORK_ROOT}/${name}")
    file(MAKE_DIRECTORY "${work_directory}")
    set_tests_properties(${name} PROPERTIES
        TIMEOUT ${timeout}
        WORKING_DIRECTORY "${work_directory}")
endfunction()

mucom88_add_test_executable(editor_core_test editor_core_test.cpp)
add_test(NAME editor_core_test COMMAND editor_core_test)
mucom88_set_test_properties(editor_core_test 30)

mucom88_add_test_executable(document_service_test test_document_service.cpp)
add_test(NAME document_service_test COMMAND document_service_test)
mucom88_set_test_properties(document_service_test 30)

mucom88_add_test_executable(editor_line_model_test test_editor_line_model.cpp)
add_test(NAME editor_line_model_test COMMAND editor_line_model_test)
mucom88_set_test_properties(editor_line_model_test 15)

mucom88_add_test_executable(editor_command_test test_editor_command.cpp)
add_test(NAME editor_command_test COMMAND editor_command_test)
mucom88_set_test_properties(editor_command_test 15)

mucom88_add_test_executable(document_recovery_test test_document_recovery.cpp)
add_test(NAME document_recovery_test COMMAND document_recovery_test)
mucom88_set_test_properties(document_recovery_test 30)

mucom88_add_test_executable(voice_service_test test_voice_service.cpp)
add_test(NAME voice_service_test COMMAND voice_service_test)
mucom88_set_test_properties(voice_service_test 30)

mucom88_add_test_executable(compile_service_test test_compile_service.cpp)
add_test(NAME compile_service_test COMMAND compile_service_test)
mucom88_set_test_properties(compile_service_test 30)

mucom88_add_test_executable(operation_lifecycle_test test_operation_lifecycle.cpp)
add_test(NAME operation_lifecycle_test COMMAND operation_lifecycle_test)
mucom88_set_test_properties(operation_lifecycle_test 30)

mucom88_add_test_executable(playback_session_test test_playback_service.cpp)
add_test(NAME playback_session_test COMMAND playback_session_test)
mucom88_set_test_properties(playback_session_test 30)
set_tests_properties(playback_session_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")

mucom88_add_test_executable(playback_coordinator_test
    test_playback_coordinator.cpp)
add_test(NAME playback_coordinator_test COMMAND playback_coordinator_test)
mucom88_set_test_properties(playback_coordinator_test 45)
set_tests_properties(playback_coordinator_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")

mucom88_add_test_executable(playback_end_detection_test
    test_playback_end_detection.cpp)
add_test(NAME playback_end_detection_test COMMAND playback_end_detection_test)
mucom88_set_test_properties(playback_end_detection_test 45)
set_tests_properties(playback_end_detection_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")

mucom88_add_test_executable(monitor_snapshot_test test_monitor_snapshot.cpp)
add_test(NAME monitor_snapshot_test COMMAND monitor_snapshot_test)
mucom88_set_test_properties(monitor_snapshot_test 30)
set_tests_properties(monitor_snapshot_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")

mucom88_add_test_executable(audio_device_service_test
    test_audio_device_service.cpp)
add_test(NAME audio_device_service_test COMMAND audio_device_service_test)
mucom88_set_test_properties(audio_device_service_test 30)
set_tests_properties(audio_device_service_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")

mucom88_add_test_executable(audio_fade_test test_audio_fade.cpp)
add_test(NAME audio_fade_test COMMAND audio_fade_test)
mucom88_set_test_properties(audio_fade_test 15)

mucom88_add_test_executable(playback_transport_stress_test
    test_playback_transport_stress.cpp)
add_test(NAME playback_transport_stress_test COMMAND playback_transport_stress_test)
mucom88_set_test_properties(playback_transport_stress_test 90)
set_tests_properties(playback_transport_stress_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")

mucom88_add_test_executable(export_service_test test_export_service.cpp)
add_test(NAME export_service_test COMMAND export_service_test)
mucom88_set_test_properties(export_service_test 60)

mucom88_add_test_executable(app_service_lifetime_test
    test_app_service_lifetime.cpp)
add_test(NAME app_service_lifetime_test COMMAND app_service_lifetime_test)
mucom88_set_test_properties(app_service_lifetime_test 30)
set_tests_properties(app_service_lifetime_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")

mucom88_add_test_executable(mub_validation_test test_mub.cpp)
add_test(NAME mub_validation_test COMMAND mub_validation_test)
mucom88_set_test_properties(mub_validation_test 15)

add_executable(audiobuffer_test
    test_audiobuffer.cpp
    ${CMAKE_SOURCE_DIR}/sdl/audiobuffer.cpp)
target_include_directories(audiobuffer_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME audiobuffer_test COMMAND audiobuffer_test)
mucom88_set_test_properties(audiobuffer_test 15)

if(NOT WIN32)
    add_executable(codeconv_test
        test_codeconv.cpp
        ${CMAKE_SOURCE_DIR}/utils/codeconv/codeconv_iconv.cpp)
    target_include_directories(codeconv_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(codeconv_test PRIVATE Iconv::Iconv)
    target_compile_definitions(codeconv_test PRIVATE USE_ICONV=1)
    add_test(NAME codeconv_test COMMAND codeconv_test)
    mucom88_set_test_properties(codeconv_test 15)
endif()

add_executable(artifact_inspector artifact_inspector.cpp)

mucom88_add_test_executable(sdl_audio_lifecycle_test test_sdl_audio.cpp)
add_test(NAME sdl_audio_lifecycle_test COMMAND sdl_audio_lifecycle_test)
mucom88_set_test_properties(sdl_audio_lifecycle_test 20)
set_tests_properties(sdl_audio_lifecycle_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")

add_test(NAME cli_contract_test
    COMMAND ${CMAKE_COMMAND}
        -DMUCOM88_EXE=$<TARGET_FILE:mucom88>
        -DPACKAGE_DIR=${CMAKE_SOURCE_DIR}/../package
        -DWORK_DIR=${MUCOM88_TEST_WORK_ROOT}/cli_contract_test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_contract_test.cmake)
mucom88_set_test_properties(cli_contract_test 30)
set_tests_properties(cli_contract_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=mucom88-invalid-audio-driver")

add_test(NAME native_regression_test
    COMMAND ${CMAKE_COMMAND}
        -DMUCOM88_EXE=$<TARGET_FILE:mucom88>
        -DARTIFACT_INSPECTOR=$<TARGET_FILE:artifact_inspector>
        -DPROJECT_ROOT=${CMAKE_SOURCE_DIR}/..
        -DPACKAGE_DIR=${CMAKE_SOURCE_DIR}/../package
        -DWORK_DIR=${MUCOM88_TEST_WORK_ROOT}/native_regression_test
        -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DSYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}
        -DARCHITECTURE=${CMAKE_SYSTEM_PROCESSOR}
        -DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}
        -DCXX_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}
        -DSDL_VERSION=${SDL2_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/native_regression_test.cmake)
mucom88_set_test_properties(native_regression_test 90)
set_tests_properties(native_regression_test PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=mucom88-invalid-audio-driver")
