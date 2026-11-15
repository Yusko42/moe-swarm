add_library(neighbor STATIC join_options.cpp join_command.cpp)
target_include_directories(neighbor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
    target_link_libraries(neighbor PRIVATE iphlpapi ws2_32)
endif()