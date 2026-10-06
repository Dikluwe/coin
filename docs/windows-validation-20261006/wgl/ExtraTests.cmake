find_package(Threads REQUIRED)
if(EXISTS "${PROJECT_SOURCE_DIR}/testsuite/glglue-lifetime/LifetimeTest.cpp")
  add_executable(GLGlueLifetimeWGL "${COIN_WINDOWS_PROBES}/LifetimeWGL.cpp")
  target_compile_definitions(GLGlueLifetimeWGL PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(GLGlueLifetimeWGL PRIVATE
    ${COIN_WINDOWS_PROBES} ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/src ${PROJECT_BINARY_DIR}/include ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(GLGlueLifetimeWGL Coin Threads::Threads ${COIN_TARGET_LINK_LIBRARIES} opengl32 gdi32 user32)
  add_test(NAME GLGlueLifetimeWGL COMMAND GLGlueLifetimeWGL)
  add_test(NAME GLGlueLifetimeWGLNoCallbacks COMMAND GLGlueLifetimeWGL --no-callbacks)
  set_tests_properties(GLGlueLifetimeWGL GLGlueLifetimeWGLNoCallbacks PROPERTIES TIMEOUT 60)
  if(MSVC AND NOT COIN_BUILD_SHARED_LIBS)
    add_executable(GLGlueLifetimeWGLAsan "${COIN_WINDOWS_PROBES}/LifetimeWGL.cpp"
      ${PROJECT_SOURCE_DIR}/src/glue/gl.cpp
      ${PROJECT_SOURCE_DIR}/src/misc/SoContextHandler.cpp)
    target_compile_definitions(GLGlueLifetimeWGLAsan PRIVATE COIN_INTERNAL HAVE_CONFIG_H COIN_WGL_ASAN COIN_DEBUG=0)
    target_compile_options(GLGlueLifetimeWGLAsan PRIVATE /fsanitize=address /Zi)
    target_link_options(GLGlueLifetimeWGLAsan PRIVATE /INCREMENTAL:NO /DEBUG)
    target_include_directories(GLGlueLifetimeWGLAsan PRIVATE
      ${COIN_WINDOWS_PROBES} ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include
      ${PROJECT_BINARY_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include/Inventor/annex
      ${COIN_TARGET_INCLUDE_DIRECTORIES})
    target_link_libraries(GLGlueLifetimeWGLAsan Coin Threads::Threads ${COIN_TARGET_LINK_LIBRARIES} opengl32 gdi32 user32)
    add_custom_command(TARGET GLGlueLifetimeWGLAsan POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll"
      "$<TARGET_FILE_DIR:GLGlueLifetimeWGLAsan>")
    add_test(NAME GLGlueLifetimeWGLAsan COMMAND GLGlueLifetimeWGLAsan)
    add_test(NAME GLGlueLifetimeWGLAsanNoCallbacks COMMAND GLGlueLifetimeWGLAsan --no-callbacks)
    set_tests_properties(GLGlueLifetimeWGLAsan GLGlueLifetimeWGLAsanNoCallbacks PROPERTIES TIMEOUT 60)
  endif()
endif()
if(EXISTS "${PROJECT_SOURCE_DIR}/testsuite/gl-context-maps/GLContextMapsTest.cpp" AND NOT COIN_BUILD_SHARED_LIBS)
  add_executable(GLContextMapsWGL "${COIN_WINDOWS_PROBES}/MapsWGL.cpp"
    ${PROJECT_SOURCE_DIR}/src/rendering/SoVBO.cpp
    ${PROJECT_SOURCE_DIR}/src/shaders/SoGLSLShaderProgram.cpp
    ${PROJECT_SOURCE_DIR}/src/shaders/SoShaderObject.cpp
    ${PROJECT_SOURCE_DIR}/src/shaders/SoShaderParameter.cpp)
  target_compile_definitions(GLContextMapsWGL PRIVATE COIN_INTERNAL HAVE_CONFIG_H COIN_SMALLMAP_TESTING)
  target_include_directories(GLContextMapsWGL PRIVATE
    ${COIN_WINDOWS_PROBES} ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include/Inventor/annex
    ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(GLContextMapsWGL Coin ${COIN_TARGET_LINK_LIBRARIES} opengl32 gdi32 user32)
  add_test(NAME GLContextMapsWGL COMMAND GLContextMapsWGL)
  set_tests_properties(GLContextMapsWGL PROPERTIES TIMEOUT 60)
endif()
