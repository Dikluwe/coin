add_executable(CoinLineTextureWeightsTest LineTextureWeightsTest.cpp)
target_link_libraries(CoinLineTextureWeightsTest PRIVATE Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(CoinLineTextureWeightsTest PRIVATE
  ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
  ${COIN_TARGET_INCLUDE_DIRECTORIES})
foreach(line_case lines controls degenerate)
  add_test(NAME LineTextureWeights_${line_case}
    COMMAND CoinLineTextureWeightsTest ${line_case})
  set_tests_properties(LineTextureWeights_${line_case} PROPERTIES TIMEOUT 30)
endforeach()
