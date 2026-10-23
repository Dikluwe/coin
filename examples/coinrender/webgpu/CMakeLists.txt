cmake_minimum_required(VERSION 3.16)
project(CoinRenderWebGPU LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Use build-coin-render-webgpu.py with Emscripten")
endif()
get_filename_component(COIN_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_subdirectory("${COIN_SOURCE_ROOT}" coin)
add_executable(coin_capture capture.cpp)
target_compile_features(coin_capture PRIVATE cxx_std_17)
target_compile_definitions(coin_capture PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
target_include_directories(coin_capture PRIVATE "${COIN_SOURCE_ROOT}/src" "${CMAKE_CURRENT_BINARY_DIR}/coin/src")
target_link_libraries(coin_capture PRIVATE CoinRender Coin)
set_target_properties(coin_capture PROPERTIES SUFFIX ".js")
target_link_options(coin_capture PRIVATE
  "-sMODULARIZE=1" "-sEXPORT_ES6=1" "-sEXPORT_NAME=createCoinCapture"
  "-sENVIRONMENT=web" "-sALLOW_MEMORY_GROWTH=1" "-sFILESYSTEM=0"
  "-sEXPORTED_FUNCTIONS=['_coin_capture','_coin_vertices','_coin_vertex_count','_coin_indices','_coin_index_count','_coin_error']"
  "-sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAPU32','UTF8ToString']")
