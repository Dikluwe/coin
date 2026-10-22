# Static VRML renderer semantics; the application still owns viewer/event behavior.
if(TARGET CoinRender AND HAVE_VRML97)
  add_executable(CoinRenderVrmlVisualTest CoinRenderVrmlVisualTest.cpp)
  target_compile_definitions(CoinRenderVrmlVisualTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderVrmlVisualTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderVrmlVisualTest CoinRender Coin)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderVrmlVisualTest PRIVATE COIN_VRML_VISUAL_X11)
    target_include_directories(CoinRenderVrmlVisualTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CoinRenderVrmlVisualTest ${X11_LIBRARIES})
  endif()
  foreach(_vrml_case material texture lights fog background geometry invalid passive)
    add_test(NAME CoinRenderVrmlCpu_${_vrml_case}
      COMMAND CoinRenderVrmlVisualTest --cpu --scenario ${_vrml_case})
    set_tests_properties(CoinRenderVrmlCpu_${_vrml_case} PROPERTIES TIMEOUT 120)
    if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
      add_test(NAME CoinRenderVrmlGpu_${_vrml_case}
        COMMAND CoinRenderVrmlVisualTest --gpu --scenario ${_vrml_case})
      set_tests_properties(CoinRenderVrmlGpu_${_vrml_case} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endif()
  endforeach()
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    foreach(_vrml_case material background)
      add_test(NAME CoinRenderVrmlMsaa2_${_vrml_case}
        COMMAND CoinRenderVrmlVisualTest --gpu --samples 2 --scenario ${_vrml_case})
      set_tests_properties(CoinRenderVrmlMsaa2_${_vrml_case} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
    foreach(_vrml_case texture lights)
      add_test(NAME CoinRenderVrmlMsaa4_${_vrml_case}
        COMMAND CoinRenderVrmlVisualTest --gpu --samples 4 --scenario ${_vrml_case})
      set_tests_properties(CoinRenderVrmlMsaa4_${_vrml_case} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
    foreach(_vrml_route staged direct)
      foreach(_vrml_case material texture fog background invalid)
        add_test(NAME CoinRenderVrmlRtt_${_vrml_route}_${_vrml_case}
          COMMAND CoinRenderVrmlVisualTest --gpu --domain ${_vrml_route} --scenario ${_vrml_case})
        set_tests_properties(CoinRenderVrmlRtt_${_vrml_route}_${_vrml_case} PROPERTIES
          TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      endforeach()
    endforeach()
    foreach(_vrml_case material texture background)
      add_test(NAME CoinRenderVrmlAsync_${_vrml_case}
        COMMAND CoinRenderVrmlVisualTest --gpu --domain async --scenario ${_vrml_case})
      set_tests_properties(CoinRenderVrmlAsync_${_vrml_case} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
    if(UNIX AND NOT APPLE AND X11_FOUND)
      foreach(_vrml_case material lights background)
        add_test(NAME CoinRenderVrmlWindow_${_vrml_case}
          COMMAND CoinRenderVrmlVisualTest --gpu --domain window --scenario ${_vrml_case})
        set_tests_properties(CoinRenderVrmlWindow_${_vrml_case} PROPERTIES
          TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      endforeach()
    endif()
  endif()
endif()
