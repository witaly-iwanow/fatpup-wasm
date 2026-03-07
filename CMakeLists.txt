cmake_minimum_required(VERSION 3.14)
project(fatpup-wasm LANGUAGES CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This project is intended to be built with Emscripten (emcmake).")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_TESTS OFF CACHE BOOL "Build fatpup tests" FORCE)
set(BUILD_UCI OFF CACHE BOOL "Build fatpup UCI binary" FORCE)
add_subdirectory(third_party/fatpup)

add_executable(fatpup_wasm src/fatpup_wasm.cpp)
target_link_libraries(fatpup_wasm PRIVATE fatpup)
target_include_directories(fatpup_wasm PRIVATE third_party/fatpup/include)

set_target_properties(fatpup_wasm PROPERTIES
    OUTPUT_NAME "fatpup_wasm"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
)

target_link_options(fatpup_wasm PRIVATE
    "-O3"
    "-sWASM=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1"
    "-sENVIRONMENT=web"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_fp_init','_fp_command','_fp_get_state_json']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
)

add_custom_command(TARGET fatpup_wasm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/web/index.html
        $<TARGET_FILE_DIR:fatpup_wasm>/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/web/styles.css
        $<TARGET_FILE_DIR:fatpup_wasm>/styles.css
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/web/app.js
        $<TARGET_FILE_DIR:fatpup_wasm>/app.js
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/web/assets
        $<TARGET_FILE_DIR:fatpup_wasm>/assets
)
