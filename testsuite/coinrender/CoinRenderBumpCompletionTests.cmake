if(TARGET CoinRenderBumpProfileTest AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(completion_target CoinRenderBumpCompletionTest CoinRenderBumpCompletionRttTest CoinRenderBumpCompletionShadowTest)
    add_executable(${completion_target} ${completion_target}.cpp
      ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
      ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
    target_compile_definitions(${completion_target} PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(${completion_target} CoinRender Coin)
    target_include_directories(${completion_target} PRIVATE
      ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  endforeach()
  add_test(NAME CoinRenderBumpCompletionCore COMMAND CoinRenderBumpCompletionTest --cpu)
  set_tests_properties(CoinRenderBumpCompletionCore PROPERTIES TIMEOUT 60)
  function(coin_bump_completion_case name executable)
    add_test(NAME ${name} COMMAND ${executable} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endfunction()
  foreach(completion_samples 1 2 4)
    coin_bump_completion_case(CoinRenderBumpCompletionLighting${completion_samples}
      CoinRenderBumpCompletionTest --mode lighting --samples ${completion_samples})
  endforeach()
  coin_bump_completion_case(CoinRenderBumpCompletionLighting4SrgbPortable
    CoinRenderBumpCompletionTest --mode lighting --samples 4 --srgb --portable-sampling)
  coin_bump_completion_case(CoinRenderBumpCompletionSpatial1
    CoinRenderBumpCompletionTest --mode spatial --samples 1)
  coin_bump_completion_case(CoinRenderBumpCompletionSpatial4SrgbPortable
    CoinRenderBumpCompletionTest --mode spatial --samples 4 --srgb --portable-sampling)
  coin_bump_completion_case(CoinRenderBumpCompletionCurrent1
    CoinRenderBumpCompletionTest --mode current --samples 1)
  coin_bump_completion_case(CoinRenderBumpCompletionCurrent4Portable
    CoinRenderBumpCompletionTest --mode current --samples 4 --portable-sampling)
  coin_bump_completion_case(CoinRenderBumpCompletionTransition1
    CoinRenderBumpCompletionTest --mode transition --samples 1)
  foreach(completion_mode models mips occurrences)
    coin_bump_completion_case(CoinRenderBumpCompletion_${completion_mode}_1
      CoinRenderBumpCompletionTest --mode ${completion_mode} --samples 1)
    coin_bump_completion_case(CoinRenderBumpCompletion_${completion_mode}_4SrgbPortable
      CoinRenderBumpCompletionTest --mode ${completion_mode} --samples 4 --srgb --portable-sampling)
  endforeach()
  coin_bump_completion_case(CoinRenderBumpCompletionShadow1
    CoinRenderBumpCompletionShadowTest --samples 1)
  coin_bump_completion_case(CoinRenderBumpCompletionShadow4Portable
    CoinRenderBumpCompletionShadowTest --samples 4 --portable-sampling)
  if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    target_compile_definitions(CoinRenderBumpCompletionTest PRIVATE COIN_BUMP_COMPLETION_RUST)
    coin_bump_completion_case(CoinRenderBumpCompletionRaw67
      CoinRenderBumpCompletionTest --mode raw --samples 1)
  endif()
  foreach(completion_route direct staged)
    coin_bump_completion_case(CoinRenderBumpCompletionRtt_${completion_route}_1
      CoinRenderBumpCompletionRttTest --${completion_route} --samples 1)
    coin_bump_completion_case(CoinRenderBumpCompletionRtt_${completion_route}_4SrgbPortable
      CoinRenderBumpCompletionRttTest --${completion_route} --samples 4 --srgb --portable-sampling)
  endforeach()
  if(UNIX AND NOT APPLE AND X11_FOUND)
    add_executable(CoinRenderBumpCompletionWindowTest CoinRenderBumpCompletionWindowTest.cpp
      ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
      ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
    target_compile_definitions(CoinRenderBumpCompletionWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(CoinRenderBumpCompletionWindowTest CoinRender Coin ${X11_LIBRARIES})
    target_include_directories(CoinRenderBumpCompletionWindowTest PRIVATE
      ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
    coin_bump_completion_case(CoinRenderBumpCompletionWindow1
      CoinRenderBumpCompletionWindowTest --samples 1 --family 1)
    coin_bump_completion_case(CoinRenderBumpCompletionWindow2Readback
      CoinRenderBumpCompletionWindowTest --samples 2 --family 2 --readback)
    coin_bump_completion_case(CoinRenderBumpCompletionWindow4SrgbPortable
      CoinRenderBumpCompletionWindowTest --samples 4 --family 1 --srgb --readback --portable-sampling)
  endif()
endif()
