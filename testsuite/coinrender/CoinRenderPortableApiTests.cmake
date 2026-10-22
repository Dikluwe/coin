if(TARGET CoinRender)
  foreach(_portable_target CoinRenderPortableActionApiTest CoinRenderLegacyConsumerApiTest)
    add_executable(${_portable_target} ${_portable_target}.cpp)
    target_compile_definitions(${_portable_target} PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(${_portable_target} CoinRender Coin)
    target_include_directories(${_portable_target} PRIVATE
      ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  endforeach()
  foreach(_portable_case callbacks abort sorting depth update invalidate)
    add_test(NAME CoinRenderPortableCpu_${_portable_case}
      COMMAND CoinRenderPortableActionApiTest --cpu --${_portable_case})
    set_tests_properties(CoinRenderPortableCpu_${_portable_case} PROPERTIES TIMEOUT 120)
  endforeach()
  add_test(NAME CoinRenderLegacyConsumerCpu COMMAND CoinRenderLegacyConsumerApiTest --cpu)
  set_tests_properties(CoinRenderLegacyConsumerCpu PROPERTIES TIMEOUT 120)
  add_test(NAME CoinRenderLegacyConsumerNative COMMAND CoinRenderLegacyConsumerApiTest --cpu --native)
  set_tests_properties(CoinRenderLegacyConsumerNative PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
    ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
  add_library(CoinRenderPortableConsumerCxx11 OBJECT CoinRenderPortableConsumerCxx11.cpp)
  set_target_properties(CoinRenderPortableConsumerCxx11 PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
  target_include_directories(CoinRenderPortableConsumerCxx11 PRIVATE
    ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include ${COIN_TARGET_INCLUDE_DIRECTORIES})
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    foreach(_portable_case callbacks abort sorting depth update invalidate tickets)
      add_test(NAME CoinRenderPortableGpu_${_portable_case}
        COMMAND CoinRenderPortableActionApiTest --gpu --${_portable_case})
      set_tests_properties(CoinRenderPortableGpu_${_portable_case} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
    add_test(NAME CoinRenderLegacyConsumerGpu COMMAND CoinRenderLegacyConsumerApiTest --gpu)
    set_tests_properties(CoinRenderLegacyConsumerGpu PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()
