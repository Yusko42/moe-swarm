find_package(CLI11 REQUIRED)

add_subdirectory(initiator)
add_subdirectory(neighbor)

add_executable(swarm main.cpp)
target_link_libraries(swarm PRIVATE CLI11::CLI11 initiator)
target_link_libraries(swarm PRIVATE CLI11::CLI11 neighbor)
