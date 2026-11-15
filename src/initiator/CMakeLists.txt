add_library(initiator STATIC create_options.cpp create_command.cpp)
target_include_directories(initiator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(initiator PRIVATE CLI11::CLI11)