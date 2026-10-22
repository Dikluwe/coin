# Public RGBA8 sample resolve, independent ticket snapshots and finite admission.
if(TARGET CoinRender AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_executable(CoinRenderMsaaAsyncCompletionTest CoinRenderMsaaAsyncCompletionTest.cpp)
  target_compile_definitions(CoinRenderMsaaAsyncCompletionTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderMsaaAsyncCompletionTest CoinRender Coin)
  target_include_directories(CoinRenderMsaaAsyncCompletionTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
    ${COIN_TARGET_INCLUDE_DIRECTORIES})
  function(coin_msaa_async_case name)
    add_test(NAME CoinRenderMsaaAsync_${name} COMMAND CoinRenderMsaaAsyncCompletionTest ${ARGN})
    set_tests_properties(CoinRenderMsaaAsync_${name} PROPERTIES TIMEOUT 120
      RESOURCE_LOCK coinrender_gpu LABELS "coinrender_gpu;msaa_async")
  endfunction()
  foreach(_r27_samples 1 2 4)
    coin_msaa_async_case(Tickets${_r27_samples} --mode tickets --samples ${_r27_samples})
    coin_msaa_async_case(Admission${_r27_samples} --mode admission --samples ${_r27_samples})
    set_tests_properties(CoinRenderMsaaAsync_Admission${_r27_samples} PROPERTIES TIMEOUT 180)
    foreach(_r27_family plain texture blend weighted peeling layers multipass)
      coin_msaa_async_case(Graph_${_r27_family}_${_r27_samples}
        --mode graph --family ${_r27_family} --samples ${_r27_samples})
    endforeach()
    foreach(_r27_route direct staged)
      coin_msaa_async_case(Graph_rtt_${_r27_route}_${_r27_samples}
        --mode graph --family rtt --${_r27_route} --samples ${_r27_samples})
      set_tests_properties(CoinRenderMsaaAsync_Graph_rtt_${_r27_route}_${_r27_samples} PROPERTIES TIMEOUT 180)
    endforeach()
    if(COIN_RENDER_BACKEND STREQUAL "BGFX")
      foreach(_r27_depth 1 2 3)
        coin_msaa_async_case(Pipeline${_r27_depth}_${_r27_samples}
          --mode pipeline --pipeline-depth ${_r27_depth} --samples ${_r27_samples})
      endforeach()
    endif()
  endforeach()
endif()
