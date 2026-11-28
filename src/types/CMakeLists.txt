add_library(swarm_types INTERFACE)
add_library(swarm::types ALIAS swarm_types)

target_include_directories(swarm_types INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(swarm_types INTERFACE cxx_std_20)
