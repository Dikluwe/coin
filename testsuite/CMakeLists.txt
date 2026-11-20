include(cc-list/Tests.cmake)
include(glglue-lifetime/Tests.cmake)
include(action-apply-exceptions/Tests.cmake)
include(gl-render-exceptions/Tests.cmake)
include(scene-texture-scene-policy/Tests.cmake)
include(SbSmallMapTests.cmake)

if(COIN_BUILD_LEGACY_GL_RENDERER)
  find_package(Threads REQUIRED)
  add_executable(CoinBumpProgramTest bumprender/FailureTest.cpp)
  target_compile_definitions(CoinBumpProgramTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinBumpProgramTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/src
    ${PROJECT_SOURCE_DIR}/include/Inventor/annex ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinBumpProgramTest Coin Threads::Threads ${COIN_TARGET_LINK_LIBRARIES})
  add_test(NAME BumpProgramFailuresAndLifetime COMMAND CoinBumpProgramTest)

  if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND AND HAVE_GLX)
      add_executable(CoinBumpGLXTest bumprender/GLXTest.cpp)
      target_compile_definitions(CoinBumpGLXTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
      target_include_directories(CoinBumpGLXTest PRIVATE
        ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/src
        ${PROJECT_SOURCE_DIR}/include/Inventor/annex
        ${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
      target_link_libraries(CoinBumpGLXTest Coin ${COIN_TARGET_LINK_LIBRARIES} ${X11_LIBRARIES})
      add_executable(CoinShadowDirectionalGLXTest shadows/DirectionalGLXTest.cpp)
      target_include_directories(CoinShadowDirectionalGLXTest PRIVATE
        ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/include/Inventor/annex
        ${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
      target_link_libraries(CoinShadowDirectionalGLXTest
        Coin ${COIN_TARGET_LINK_LIBRARIES} ${X11_LIBRARIES})
      foreach(shadow_case low medium high matrix boundaries)
        add_test(NAME ShadowDirectionalGLX_${shadow_case}
          COMMAND CoinShadowDirectionalGLXTest ${shadow_case})
        set_tests_properties(ShadowDirectionalGLX_${shadow_case} PROPERTIES
          SKIP_RETURN_CODE 77 LABELS "shadows;native-gl" TIMEOUT 120)
      endforeach()
      add_executable(CoinShadowNegativeSpotGLXTest shadows/NegativeSpotGLXTest.cpp)
      target_include_directories(CoinShadowNegativeSpotGLXTest PRIVATE
        ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/include/Inventor/annex
        ${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
      target_link_libraries(CoinShadowNegativeSpotGLXTest
        Coin ${COIN_TARGET_LINK_LIBRARIES} ${X11_LIBRARIES})
      foreach(shadow_case low medium high boundaries)
        add_test(NAME ShadowNegativeSpotGLX_${shadow_case}
          COMMAND CoinShadowNegativeSpotGLXTest ${shadow_case})
        set_tests_properties(ShadowNegativeSpotGLX_${shadow_case} PROPERTIES
          SKIP_RETURN_CODE 77 LABELS "shadows;native-gl" TIMEOUT 120)
      endforeach()
