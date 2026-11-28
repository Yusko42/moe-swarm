find_package(CLI11 REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(initiator STATIC
    create_options.cpp
    create_command.cpp
    registry/node_registry.cpp
    http/http_server.cpp
)
target_include_directories(initiator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(initiator
    PUBLIC swarm::types
    PRIVATE CLI11::CLI11
            httplib::httplib
            nlohmann_json::nlohmann_json
)
