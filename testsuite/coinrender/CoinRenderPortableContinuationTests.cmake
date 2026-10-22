if(TARGET CoinRender)
  foreach(_continuation_target CoinRenderSeparateFacesApiTest CoinRenderSmoothingApiTest CoinRenderPassSnapshotApiTest)
    add_executable(${_continuation_target} ${_continuation_target}.cpp)
    target_compile_definitions(${_continuation_target} PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(${_continuation_target} CoinRender Coin)
    target_include_directories(${_continuation_target} PRIVATE
      ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
    add_test(NAME ${_continuation_target}_Cpu COMMAND ${_continuation_target} --cpu)
    set_tests_properties(${_continuation_target}_Cpu PROPERTIES TIMEOUT 120)
    if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
      add_test(NAME ${_continuation_target}_Gpu COMMAND ${_continuation_target} --gpu)
      set_tests_properties(${_continuation_target}_Gpu PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endif()
  endforeach()
endif()
