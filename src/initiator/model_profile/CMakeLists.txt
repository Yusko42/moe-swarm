add_subdirectory(gguf_reader)
add_library(model_profile STATIC model_profile.cpp model_profile_cli.cpp)
target_include_directories(model_profile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(model_profile PUBLIC gguf_reader)
