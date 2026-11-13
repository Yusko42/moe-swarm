cmake_minimum_required(VERSION 3.22)

project(moe-swarm
    VERSION 0.0.1
    DESCRIPTION "Distributed systems and the launch of large Mixture-of-Experts architecture language models on PCs"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SWARM_WERROR "Treat compiler warnings as errors" OFF)
option(SWARM_BUILD_TESTS "Build tests" ON)
option(SWARM_BUILD_LLAMA "Build llama.cpp tools" ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()


if(SWARM_WERROR)
    message(STATUS "SWARM_WERROR: warnings are treated as errors")
    if(MSVC)
        add_compile_options(/WX)
    else()
        add_compile_options(-Werror)
    endif()
endif()

# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# foreach(cfg IN ITEMS DEBUG RELEASE)
#     set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/bin")
# endforeach()

if(SWARM_BUILD_LLAMA)
    include(ExternalProject)

    set(LLAMA_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/llama")
    set(LLAMA_CPP_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/llama_cpp_build")

    ExternalProject_Add(llama_cpp_project
        SOURCE_DIR          "${LLAMA_CPP_SOURCE_DIR}"
        BINARY_DIR          "${LLAMA_CPP_BINARY_DIR}"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DGGML_RPC=ON
            -DCMAKE_CXX_COMPILER_LAUNCHER:STRING=${CMAKE_CXX_COMPILER_LAUNCHER}
            -DCMAKE_C_COMPILER_LAUNCHER:STRING=${CMAKE_C_COMPILER_LAUNCHER}
        UPDATE_COMMAND      ""
        INSTALL_COMMAND     ""
        BUILD_BYPRODUCTS
            "${LLAMA_CPP_BINARY_DIR}/bin/llama-cli"
            "${LLAMA_CPP_BINARY_DIR}/bin/rpc-server"
    )
endif()

add_subdirectory(src ${CMAKE_CURRENT_BINARY_DIR}/bin)

if(SWARM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Copying compile_commands.json to source root"
        VERBATIM
    )
endif()
