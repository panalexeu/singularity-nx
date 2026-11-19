# singularity-recomp - the native (deko3d) Switch build. From gears2-nx's gow2/CMakeLists.txt (GPL-3.0).
#
# Configure (tools/switch/build-native.sh does this in Docker):
#   cmake -S switch -B out/build/switch-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=tools/switch/switch-devkitA64.cmake -DREXGLUE_SWITCH_DEKO3D=ON \
#     -DREXSDK_DIR=<rexglue-sdk-nx> -DSINGULARITY_CODEGEN_DIR=out/codegen-nx
cmake_minimum_required(VERSION 3.25)
project(singularity LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/singularity_switch.cmake)

if(NOT REXGLUE_SWITCH_DEKO3D)
    message(FATAL_ERROR "singularity: the native build is deko3d only (-DREXGLUE_SWITCH_DEKO3D=ON)")
endif()

set(SINGULARITY_SHADER_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/switch/shaders")

add_executable(singularity
    src/main.cpp
    src/singularity_gpu.cpp
    src/singularity_shaders.cpp
    src/singularity_cpu.cpp
    src/singularity_guards.cpp
    src/singularity_game_patches.cpp
    src/singularity_perf_hooks.cpp
    src/singularity_pgo.cpp
    ${SINGULARITY_SHADER_TOOLS}/ds_shader_library.cpp
)
singularity_setup_target(singularity)

# The guest's mtmsrd regions as one global mutex instead of per thread (gears2-nx's option, off).
option(SINGULARITY_MSR_GLOBAL "The mtmsrd regions as one global mutex by default" OFF)
if(SINGULARITY_MSR_GLOBAL)
    set_source_files_properties(src/singularity_cpu.cpp PROPERTIES COMPILE_DEFINITIONS SINGULARITY_MSR_GLOBAL_DEFAULT=1)
endif()

rexglue_switch_add_deko3d(singularity)
target_compile_definitions(singularity PRIVATE SINGULARITY_DEKO3D=1)
target_link_libraries(singularity PRIVATE mspack)
target_include_directories(singularity PRIVATE ${SINGULARITY_SHADER_TOOLS} ${REXSDK_DIR}/thirdparty/xxHash)

# Profile-guided optimisation (docs: README "Profile-guided build"). PGO=generate: an instrumented NRO that streams its
# branch counts to pgo/ on the SD card (src/singularity_pgo.cpp); tools/switch/pgo_merge.sh turns them into .gcda files in
# SINGULARITY_PGO_DIR; PGO=use builds with them. The recompiled game, this app and the SDK runtime (renderer, GPU ring,
# kernel, audio glue) are profiled; -fprofile-prefix-path makes the profile's names independent of the build folder.
set(SINGULARITY_PGO "" CACHE STRING "Profile-guided optimisation: empty, generate or use")
set(SINGULARITY_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pgo" CACHE PATH "Folder of the PGO profile (.gcda files)")
if(SINGULARITY_PGO)
    get_filename_component(_singularity_pgo_dir "${SINGULARITY_PGO_DIR}" ABSOLUTE)
    if(SINGULARITY_PGO STREQUAL "generate")
        set(_singularity_pgo "-fprofile-generate=${_singularity_pgo_dir}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                          -fno-profile-values -fprofile-update=single -fprofile-info-section=gcov_info)
        target_compile_definitions(singularity PRIVATE SINGULARITY_PGO_GENERATE=1)
        target_link_options(singularity PRIVATE "-fprofile-generate=${_singularity_pgo_dir}")
    elseif(SINGULARITY_PGO STREQUAL "use")
        file(GLOB _singularity_pgo_gcda "${_singularity_pgo_dir}/*.gcda")
        if(NOT _singularity_pgo_gcda)
            message(FATAL_ERROR "SINGULARITY_PGO=use: no profile in ${_singularity_pgo_dir} (tools/switch/pgo_merge.sh)")
        endif()
        set(_singularity_pgo "-fprofile-use=${_singularity_pgo_dir}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                          -fprofile-correction -fno-profile-values -fprofile-partial-training -Wno-missing-profile
                          -Wno-coverage-mismatch)
    else()
        message(FATAL_ERROR "SINGULARITY_PGO must be empty, generate or use (got ${SINGULARITY_PGO})")
    endif()
    foreach(_t singularity singularity_recomp rexruntime rexcore rexaudio rexfilesystem libavcodec libavutil)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${_singularity_pgo}>")
        endif()
    endforeach()
    message(STATUS "singularity: PGO ${SINGULARITY_PGO} (${_singularity_pgo_dir})")
endif()

set(SINGULARITY_VERSION "0.3")
execute_process(COMMAND git rev-parse --short=10 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE SINGULARITY_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT SINGULARITY_COMMIT)
    set(SINGULARITY_COMMIT "unknown")
endif()
target_compile_definitions(singularity PRIVATE SINGULARITY_VERSION="${SINGULARITY_VERSION}"
                                               SINGULARITY_COMMIT="${SINGULARITY_COMMIT}")

if(REXGLUE_PLATFORM_SWITCH)
    rexglue_switch_add_nro(singularity
        NAME "Singularity"
        AUTHOR "alexeu"
        VERSION "${SINGULARITY_VERSION}"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")  # 256x256 baseline JPEG
endif()
