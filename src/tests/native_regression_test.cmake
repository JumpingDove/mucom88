cmake_minimum_required(VERSION 3.20)

foreach(required MUCOM88_EXE ARTIFACT_INSPECTOR PROJECT_ROOT PACKAGE_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

function(source_snapshot output)
    file(GLOB_RECURSE snapshot_files LIST_DIRECTORIES FALSE
        "${PROJECT_ROOT}/src/*" "${PROJECT_ROOT}/package/*")
    list(SORT snapshot_files)
    set(snapshot "")
    foreach(path IN LISTS snapshot_files)
        file(RELATIVE_PATH relative "${PROJECT_ROOT}" "${path}")
        file(SHA256 "${path}" hash)
        string(APPEND snapshot "${relative} ${hash}\n")
    endforeach()
    set(${output} "${snapshot}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/run-a" "${WORK_DIR}/run-b")
source_snapshot(source_before)
file(WRITE "${WORK_DIR}/regression-environment.txt"
    "system=${SYSTEM_NAME} ${SYSTEM_VERSION}\n"
    "architecture=${ARCHITECTURE}\n"
    "compiler=${CXX_COMPILER_ID} ${CXX_COMPILER_VERSION}\n"
    "build_type=${BUILD_TYPE}\n"
    "sdl=${SDL_VERSION}\n"
    "audio_driver=$ENV{SDL_AUDIODRIVER}\n")

function(run_mucom label)
    execute_process(
        COMMAND "${MUCOM88_EXE}" ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 30)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "${label}: mucom88 failed with ${result}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    if(NOT stderr STREQUAL "")
        message(FATAL_ERROR "${label}: unexpected stderr:\n${stderr}")
    endif()
endfunction()

function(require_artifact path expected_size expected_hash)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing artifact: ${path}")
    endif()
    file(SIZE "${path}" actual_size)
    file(SHA256 "${path}" actual_hash)
    if(NOT actual_size EQUAL expected_size OR NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR
            "baseline mismatch: ${path}\n"
            "expected size/hash: ${expected_size} ${expected_hash}\n"
            "actual size/hash:   ${actual_size} ${actual_hash}")
    endif()
endfunction()

set(sampl1_input_hash 8194fd26cee9be5f60bc57b9c6c819ce89f774bee7f6640a2ba0e21b47f898e4)
set(sampl2_input_hash 0e09a4a2a475408be30e65cc28c37ace52a7d591a8b7f49e00f36ba43afed409)
set(sampl3_input_hash ff1ae3b5e3dbe8a66a165ecd286d4f71f5c3d7e36ba6f92d2c84d61506e21617)
set(sampl1_size 65647)
set(sampl2_size 3886)
set(sampl3_size 1309)
set(sampl1_hash 52116e8284f0e29d0de050b984926d6ca9da60e88cb1cb1b46d4ab586f31e309)
set(sampl2_hash e839f2121bad14cf9b901f52e1aa409f8d2088805502cb9fb9f07a9d9321c84e)
set(sampl3_hash aae62a128a1197fb66a30a3bc905b21bc177a280e4b245bc878718d0dadc0787)

foreach(sample sampl1 sampl2 sampl3)
    set(input "${PACKAGE_DIR}/${sample}.muc")
    file(SHA256 "${input}" input_hash_before)
    if(NOT input_hash_before STREQUAL "${${sample}_input_hash}")
        message(FATAL_ERROR "fixture hash mismatch: ${input}")
    endif()
    foreach(run run-a run-b)
        set(output "${WORK_DIR}/${run}/${sample}.mub")
        run_mucom("${sample}-${run}" -g -o "${output}" "${input}")
        require_artifact("${output}" ${${sample}_size} "${${sample}_hash}")
    endforeach()
endforeach()

foreach(driver mucom88 mucom88e mucom88em)
    foreach(run run-a run-b)
        set(output "${WORK_DIR}/${run}/driver-${driver}.mub")
        run_mucom("driver-${driver}-${run}" -g -f "${driver}"
            -o "${output}" "${PACKAGE_DIR}/sampl3.muc")
    endforeach()
    file(SHA256 "${WORK_DIR}/run-a/driver-${driver}.mub" hash_a)
    file(SHA256 "${WORK_DIR}/run-b/driver-${driver}.mub" hash_b)
    if(NOT hash_a STREQUAL hash_b)
        message(FATAL_ERROR "${driver} output is not deterministic")
    endif()
endforeach()

foreach(run run-a run-b)
    run_mucom("offline-${run}" -x -l 1
        -o "${WORK_DIR}/${run}/offline.mub"
        -w "${WORK_DIR}/${run}/sample1.wav"
        -b "${WORK_DIR}/${run}/sample1.vgm"
        "${PACKAGE_DIR}/sampl1.muc")
    run_mucom("s98-${run}" -x -l 1
        -o "${WORK_DIR}/${run}/offline-s98.mub"
        -b "${WORK_DIR}/${run}/sample1.s98"
        "${PACKAGE_DIR}/sampl1.muc")
    require_artifact("${WORK_DIR}/${run}/sample1.wav" 176444
        0b9e7607f5d1fe25eadd7d4d7b7872e58c977098c9e216d7542b97163c6119f2)
    require_artifact("${WORK_DIR}/${run}/sample1.vgm" 2396
        60e6e80d2f5c53207a79d05c8331652be674af936faa43408b582b8fb3e92294)
    require_artifact("${WORK_DIR}/${run}/sample1.s98" 2091
        8f4d5797a9b17bc6fda0ce17ec667a8c3e3411cf870ba83ccf9bddb419b07664)
endforeach()

run_mucom(mub-round-trip -x -l 1
    -w "${WORK_DIR}/round-trip.wav" "${WORK_DIR}/run-a/offline.mub")
require_artifact("${WORK_DIR}/round-trip.wav" 176444
    0b9e7607f5d1fe25eadd7d4d7b7872e58c977098c9e216d7542b97163c6119f2)
run_mucom(mub-round-trip-without-pcm -x -l 1
    -w "${WORK_DIR}/round-trip-no-pcm.wav" "${WORK_DIR}/run-a/sampl3.mub")

execute_process(
    COMMAND "${ARTIFACT_INSPECTOR}"
        "${WORK_DIR}/run-a/sampl1.mub"
        "${WORK_DIR}/run-a/sampl3.mub"
        "${WORK_DIR}/run-a/sample1.wav"
        "${WORK_DIR}/run-a/sample1.vgm"
        "${WORK_DIR}/run-a/sample1.s98"
    RESULT_VARIABLE inspector_result
    OUTPUT_VARIABLE inspector_stdout
    ERROR_VARIABLE inspector_stderr
    TIMEOUT 15)
if(NOT inspector_result EQUAL 0)
    message(FATAL_ERROR
        "artifact inspection failed\n${inspector_stdout}\n${inspector_stderr}")
endif()

execute_process(
    COMMAND "${ARTIFACT_INSPECTOR}"
        "${WORK_DIR}/run-a/sampl1.mub"
        "${WORK_DIR}/run-a/sampl3.mub"
        "${WORK_DIR}/round-trip-no-pcm.wav"
        "${WORK_DIR}/run-a/sample1.vgm"
        "${WORK_DIR}/run-a/sample1.s98"
    RESULT_VARIABLE no_pcm_inspector_result
    OUTPUT_VARIABLE no_pcm_inspector_stdout
    ERROR_VARIABLE no_pcm_inspector_stderr
    TIMEOUT 15)
if(NOT no_pcm_inspector_result EQUAL 0)
    message(FATAL_ERROR
        "no-PCM MUB round-trip failed\n"
        "${no_pcm_inspector_stdout}\n${no_pcm_inspector_stderr}")
endif()

set(unicode_dir "${WORK_DIR}/日本語 path/正規化-é")
file(MAKE_DIRECTORY "${unicode_dir}")
file(COPY "${PACKAGE_DIR}/sampl3.muc" "${PACKAGE_DIR}/voice.dat"
    "${PACKAGE_DIR}/mucompcm.bin" DESTINATION "${unicode_dir}")
run_mucom(unicode-and-space-path -g
    -o "${unicode_dir}/出力 file.mub" "${unicode_dir}/sampl3.muc")
require_artifact("${unicode_dir}/出力 file.mub" 1309 "${sampl3_hash}")

foreach(sample sampl1 sampl2 sampl3)
    file(SHA256 "${PACKAGE_DIR}/${sample}.muc" input_hash_after)
    if(NOT input_hash_after STREQUAL "${${sample}_input_hash}")
        message(FATAL_ERROR "source fixture changed during test: ${sample}.muc")
    endif()
endforeach()
file(SHA256 "${PACKAGE_DIR}/voice.dat" voice_hash)
file(SHA256 "${PACKAGE_DIR}/mucompcm.bin" pcm_hash)
if(NOT voice_hash STREQUAL 5a1c7121804d3e486949357d122792cb2d9cb33d18e481ae0a1a283a367c5a0f OR
   NOT pcm_hash STREQUAL 29e3a31a38388eaa7cf93fb00af85e806f393a9ea5e26342f6996c8ab4af0609)
    message(FATAL_ERROR "read-only package resource changed during test")
endif()
source_snapshot(source_after)
if(NOT source_before STREQUAL source_after)
    message(FATAL_ERROR "src/ or package/ changed during regression test")
endif()

message(STATUS "macOS native deterministic regression baseline passed")
