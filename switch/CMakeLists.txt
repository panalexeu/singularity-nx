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

set(SINGULARITY_VERSION "0.2.0")
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
        AUTHOR "singularity-recomp"
        VERSION "${SINGULARITY_VERSION}"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")  # 256x256 baseline JPEG
endif()
