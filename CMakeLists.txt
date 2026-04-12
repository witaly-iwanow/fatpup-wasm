cmake_minimum_required(VERSION 3.14)
project(fatpup-wasm LANGUAGES CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This project is intended to be built with Emscripten (emcmake).")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(FATPUP_GIT_REPOSITORY "https://github.com/witaly-iwanow/fatpup.git" CACHE STRING "fatpup git repo")
set(FATPUP_GIT_TAG "b2a2237f4274222ee5732c8d32625765da24dec9" CACHE STRING "fatpup git tag/commit")

# fatpup controls these options in its own project; force them off here.
set(BUILD_TESTS OFF CACHE BOOL "Build fatpup tests" FORCE)
set(BUILD_UCI OFF CACHE BOOL "Build fatpup UCI binary" FORCE)

FetchContent_Declare(
    fatpup
    GIT_REPOSITORY ${FATPUP_GIT_REPOSITORY}
    GIT_TAG ${FATPUP_GIT_TAG}
)
FetchContent_MakeAvailable(fatpup)

add_executable(fatpup_wasm src/fatpup_wasm.cpp)
target_link_libraries(fatpup_wasm PRIVATE fatpup)

set_target_properties(fatpup_wasm PROPERTIES
    OUTPUT_NAME "fatpup_wasm"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/docs"
)

target_link_options(fatpup_wasm PRIVATE
    "-O3"
    "-sWASM=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1"
    "-sENVIRONMENT=web"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_fp_init','_fp_command','_fp_get_state_json']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "--embed-file"
    "${CMAKE_SOURCE_DIR}/tools/opening_book.txt@/opening_book.txt"
)
