cmake_minimum_required(VERSION 3.20)

foreach(required MUCOM88_EXE PACKAGE_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(sample "${PACKAGE_DIR}/sampl3.muc")

function(expect_cli label expected_result stdout_pattern stderr_pattern)
    execute_process(
        COMMAND "${MUCOM88_EXE}" ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE actual_stdout
        ERROR_VARIABLE actual_stderr
        TIMEOUT 15)
    if(NOT actual_result EQUAL expected_result)
        message(FATAL_ERROR
            "${label}: expected exit ${expected_result}, got ${actual_result}\n"
            "stdout:\n${actual_stdout}\nstderr:\n${actual_stderr}")
    endif()
    if(stdout_pattern STREQUAL "")
        if(NOT actual_stdout STREQUAL "")
            message(FATAL_ERROR "${label}: unexpected stdout:\n${actual_stdout}")
        endif()
    elseif(NOT actual_stdout MATCHES "${stdout_pattern}")
        message(FATAL_ERROR
            "${label}: stdout does not match '${stdout_pattern}':\n${actual_stdout}")
    endif()
    if(stderr_pattern STREQUAL "")
        if(NOT actual_stderr STREQUAL "")
            message(FATAL_ERROR "${label}: unexpected stderr:\n${actual_stderr}")
        endif()
    elseif(NOT actual_stderr MATCHES "${stderr_pattern}")
        message(FATAL_ERROR
            "${label}: stderr does not match '${stderr_pattern}':\n${actual_stderr}")
    endif()
endfunction()

expect_cli(help 0 "usage: mucom88" "" -h)
expect_cli(no_arguments 2 "" "no input file specified.*usage: mucom88")
expect_cli(missing_value 2 "" "option -p requires a value.*usage: mucom88" -p)
expect_cli(unknown_option 2 "" "unknown option: -z.*usage: mucom88" -z)
expect_cli(plugin_unsupported 2 ""
    "plugins.*are not supported" -a plugin.dylib "${sample}")
expect_cli(real_chip_unsupported 2 ""
    "SCCI real-chip output.*is not supported" -s "${sample}")
expect_cli(missing_input 1 "File not found" "" -g -k
    -o "${WORK_DIR}/missing.mub" "${WORK_DIR}/missing.muc")
expect_cli(missing_pcm 1 "PCM file not found" "" -g
    -p "${WORK_DIR}/missing.bin" -o "${WORK_DIR}/missing-pcm.mub" "${sample}")
expect_cli(missing_voice 1 "Voice file not found" "" -g
    -v "${WORK_DIR}/missing.dat" -o "${WORK_DIR}/missing-voice.mub" "${sample}")
expect_cli(output_failure 1 "File write error" "" -g
    -o "${WORK_DIR}/missing-directory/out.mub" "${sample}")

# SDL_AUDIODRIVER is intentionally invalid for this whole CTest. Success proves
# that information, compile-only, and offline paths do not open an audio device.
expect_cli(info_without_audio 0 "#title Sample Music 3" "" -i "${sample}")
expect_cli(compile_without_audio 0 "#Saved" "" -g
    -o "${WORK_DIR}/compile-only.mub" "${sample}")
expect_cli(offline_without_audio 0 "#Record to" "" -x -l 1
    -o "${WORK_DIR}/offline.mub" -w "${WORK_DIR}/offline.wav" "${sample}")

message(STATUS "CLI exit-code, stream, and no-audio contract passed")
