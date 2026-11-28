add_library(gguf_reader STATIC gguf_reader.cpp)
target_include_directories(gguf_reader PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(gguf_reader PUBLIC cxx_std_20)
