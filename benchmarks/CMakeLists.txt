add_executable(CoinSbHashBenchmark SbHashBenchmark.cpp)
target_compile_definitions(CoinSbHashBenchmark PRIVATE COIN_INTERNAL)
target_link_libraries(CoinSbHashBenchmark Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(CoinSbHashBenchmark PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_BINARY_DIR}/include
  ${COIN_TARGET_INCLUDE_DIRECTORIES})

if(USE_PTHREAD)
  target_link_libraries(CoinSbHashBenchmark pthread)
endif()
