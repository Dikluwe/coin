# Legacy scalar/token declarations keep Coin node ABI usable in the Android
# CPU profile. No OpenGL library or GLES substitution is linked.
if(COIN_ANDROID_CPU_ONLY)
  foreach(_header gl.h glext.h)
    configure_file("${PROJECT_SOURCE_DIR}/src/glue/khronos/GL/${_header}"
      "${CMAKE_CURRENT_BINARY_DIR}/Inventor/system/legacy-gl/${_header}" COPYONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Inventor/system/legacy-gl/${_header}"
      DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Inventor/system/legacy-gl"
      COMPONENT development)
  endforeach()
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Inventor/C/basic.h.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/Inventor/C/basic.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Inventor/system/gl-headers.h.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/Inventor/system/gl-headers.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Inventor/system/inttypes.h.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/Inventor/system/inttypes.h)

file(GLOB_RECURSE COMMON_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/Inventor/*.h)
file(GLOB_RECURSE COMMON_BIN_HDRS ${CMAKE_CURRENT_BINARY_DIR}/Inventor/*.h)

if(NOT COIN_BUILD_RENDER)
  list(FILTER COMMON_HDRS EXCLUDE REGEX ".*/SoWgpu.*")
endif()

set(COIN_DOCUMENTATION_FILES
  ${COIN_DOCUMENTATION_FILES}
  ${CMAKE_CURRENT_SOURCE_DIR}/SoDebug.h
  ${CMAKE_CURRENT_BINARY_DIR}/Inventor/C/basic.h
  ${COMMON_HDRS}
  CACHE INTERNAL "COIN_DOCUMENTATION_FILES"
)
set(COIN_INTERNAL_DOCUMENTATION_FILES
  ${COIN_INTERNAL_DOCUMENTATION_FILES}
  ${CMAKE_CURRENT_SOURCE_DIR}/Inventor/C/glue/gl.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Inventor/C/glue/spidermonkey.h
  CACHE INTERNAL "COIN_INTERNAL_DOCUMENTATION_FILES"
)

set(COMMON_HDRS ${COMMON_HDRS} ${COMMON_BIN_HDRS} PARENT_SCOPE)
