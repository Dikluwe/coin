if(COIN_BUILD_EXAMPLES)
  add_subdirectory(dynloadextensions)
  add_subdirectory(glfw)
  add_subdirectory(qt)
  add_subdirectory(sdl3)
endif()
if(COIN_BUILD_RENDER_WINDOW_EXAMPLE)
  if(NOT COIN_BUILD_RENDER OR
     NOT COIN_RENDER_BACKEND MATCHES "^(RUST_BRIDGE|BGFX)$")
    message(FATAL_ERROR "COIN_BUILD_RENDER_WINDOW_EXAMPLE requires COIN_BUILD_RENDER=ON and RUST_BRIDGE or BGFX backend.")
  endif()
  if(NOT ((UNIX AND NOT APPLE AND X11_FOUND) OR
          (WIN32 AND COIN_RENDER_BACKEND STREQUAL "BGFX") OR
          (COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE" AND
           (ANDROID OR WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))))
    message(FATAL_ERROR "No native window example is available for this platform/backend.")
  endif()
endif()
if(COIN_BUILD_RENDER_WINDOW_EXAMPLE OR COIN_BUILD_RENDER_BENCHMARKS)
  add_subdirectory(coinrender)
endif()
