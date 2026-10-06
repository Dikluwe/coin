if(COIN_BUILD_RENDER_WINDOW_EXAMPLE)
  if(ANDROID)
    if(NOT COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
      message(FATAL_ERROR "The P23 Android smoke requires COIN_RENDER_BACKEND=RUST_BRIDGE")
    endif()
    if(ANDROID_NDK)
      set(_coin_android_ndk "${ANDROID_NDK}")
    else()
      set(_coin_android_ndk "${CMAKE_ANDROID_NDK}")
    endif()
    set(_coin_android_glue "${_coin_android_ndk}/sources/android/native_app_glue")
    if(NOT EXISTS "${_coin_android_glue}/android_native_app_glue.c")
      message(FATAL_ERROR "Android NDK native_app_glue is required for the P23 smoke")
    endif()
    add_library(coin_render_android_smoke SHARED
      coin_render_android_smoke.cpp "${_coin_android_glue}/android_native_app_glue.c")
    target_link_libraries(coin_render_android_smoke PRIVATE CoinRender Coin android log)
    target_include_directories(coin_render_android_smoke PRIVATE
      "${_coin_android_glue}"
      ${PROJECT_SOURCE_DIR}/experimental/include
      ${PROJECT_SOURCE_DIR}/include
      ${PROJECT_BINARY_DIR}/include
      ${COIN_TARGET_INCLUDE_DIRECTORIES})
  elseif(WIN32)
    if(NOT COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE" AND NOT COIN_RENDER_BACKEND STREQUAL "BGFX")
      message(FATAL_ERROR "The P21 Win32 smoke requires RUST_BRIDGE or BGFX")
    endif()
    add_executable(coin_render_win32_smoke coin_render_win32_smoke.cpp)
    target_compile_definitions(coin_render_win32_smoke PRIVATE _WIN32_WINNT=0x0A00)
    if(COIN_BUILD_TESTS)
      add_test(NAME CoinRenderWin32D3D12Smoke COMMAND coin_render_win32_smoke)
      set_tests_properties(CoinRenderWin32D3D12Smoke PROPERTIES TIMEOUT 60 ENVIRONMENT "WGPU_BACKEND=dx12;COIN_BGFX_RENDERER=d3d12")
      add_test(NAME CoinRenderWin32VulkanSmoke COMMAND coin_render_win32_smoke --vulkan)
      set_tests_properties(CoinRenderWin32VulkanSmoke PROPERTIES TIMEOUT 60 ENVIRONMENT "WGPU_BACKEND=vulkan;COIN_BGFX_RENDERER=vulkan")
    endif()
    target_link_libraries(coin_render_win32_smoke PRIVATE CoinRender Coin user32)
    target_include_directories(coin_render_win32_smoke PRIVATE
      ${PROJECT_SOURCE_DIR}/experimental/include
      ${PROJECT_SOURCE_DIR}/include
      ${PROJECT_BINARY_DIR}/include
      ${COIN_TARGET_INCLUDE_DIRECTORIES})
  elseif(APPLE)
    if(NOT COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
      message(FATAL_ERROR "The P22 AppKit smoke requires COIN_RENDER_BACKEND=RUST_BRIDGE")
    endif()
    enable_language(OBJCXX)
    add_executable(coin_render_appkit_smoke coin_render_appkit_smoke.mm)
    target_compile_options(coin_render_appkit_smoke PRIVATE -fobjc-arc)
    target_link_libraries(coin_render_appkit_smoke PRIVATE CoinRender Coin
      "-framework Cocoa" "-framework QuartzCore" "-framework Metal")
    target_include_directories(coin_render_appkit_smoke PRIVATE
      ${PROJECT_SOURCE_DIR}/experimental/include
      ${PROJECT_SOURCE_DIR}/include
      ${PROJECT_BINARY_DIR}/include
      ${COIN_TARGET_INCLUDE_DIRECTORIES})
  else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(COIN_RENDER_GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
      endif()
      if(COIN_RENDER_GTK3_FOUND)
        add_executable(coin_render_wayland_smoke coin_render_wayland_smoke.cpp)
        target_link_libraries(coin_render_wayland_smoke PRIVATE CoinRender Coin PkgConfig::COIN_RENDER_GTK3)
        target_include_directories(coin_render_wayland_smoke PRIVATE
          ${PROJECT_SOURCE_DIR}/experimental/include
          ${PROJECT_SOURCE_DIR}/include
          ${PROJECT_BINARY_DIR}/include
          ${COIN_TARGET_INCLUDE_DIRECTORIES})
      endif()
    endif()
if(X11_FOUND)
add_executable(coin_render_window_cone coin_render_window_cone.cpp)

target_include_directories(coin_render_window_cone PRIVATE
	${PROJECT_SOURCE_DIR}/experimental/include
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
if(COIN_RENDER_BACKEND STREQUAL "BGFX")
	target_compile_definitions(coin_render_window_cone PRIVATE COIN_EXAMPLE_BGFX=1)
endif()

find_package(glfw3 3.3 QUIET)
if(glfw3_FOUND)
	target_compile_definitions(coin_render_window_cone PRIVATE HAVE_GLFW=1)
	target_link_libraries(coin_render_window_cone PRIVATE CoinRender glfw ${X11_LIBRARIES} Coin)
	target_include_directories(coin_render_window_cone PRIVATE ${X11_INCLUDE_DIR})
else()
	target_link_libraries(coin_render_window_cone PRIVATE CoinRender ${X11_LIBRARIES} Coin)
	target_include_directories(coin_render_window_cone PRIVATE ${X11_INCLUDE_DIR})
endif()

add_executable(coin_render_viewer coin_render_viewer.cpp)
target_link_libraries(coin_render_viewer PRIVATE CoinRender ${X11_LIBRARIES} Coin)
target_include_directories(coin_render_viewer PRIVATE
	${PROJECT_SOURCE_DIR}/experimental/include
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${X11_INCLUDE_DIR}
	${COIN_TARGET_INCLUDE_DIRECTORIES})

if(COIN_BUILD_LEGACY_GL_RENDERER)
	add_executable(coin_render_window_benchmark coin_render_window_benchmark.cpp)
	set(_coin_render_window_gl_libraries ${OPENGL_gl_LIBRARY} ${OPENGL_glx_LIBRARY})
	if(TARGET OpenGL::GL)
		list(APPEND _coin_render_window_gl_libraries OpenGL::GL)
	endif()
	if(TARGET OpenGL::GLX)
		list(APPEND _coin_render_window_gl_libraries OpenGL::GLX)
	endif()
	target_link_libraries(coin_render_window_benchmark PRIVATE CoinRender
		${X11_LIBRARIES} ${_coin_render_window_gl_libraries} Coin)
	target_include_directories(coin_render_window_benchmark PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${X11_INCLUDE_DIR}
		${COIN_TARGET_INCLUDE_DIRECTORIES})
endif()
endif()
  endif()
endif()

if(COIN_BUILD_LEGACY_GL_RENDERER)
	add_executable(coin_render_gl_benchmark coin_render_gl_benchmark.cpp)
	target_link_libraries(coin_render_gl_benchmark PRIVATE CoinRender Coin)
	target_include_directories(coin_render_gl_benchmark PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_executable(coin_render_freecad_compare coin_render_freecad_compare.cpp)
	target_link_libraries(coin_render_freecad_compare PRIVATE CoinRender Coin)
	target_include_directories(coin_render_freecad_compare PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
endif()

if(COIN_INSTALL_RENDER_EXPERIMENTAL)
    if(TARGET coin_render_android_smoke)
        install(TARGETS coin_render_android_smoke
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime)
    endif()
    if(TARGET coin_render_appkit_smoke)
        set_target_properties(coin_render_appkit_smoke PROPERTIES
            INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
        install(TARGETS coin_render_appkit_smoke
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    endif()
    if(TARGET coin_render_wayland_smoke)
        set_target_properties(coin_render_wayland_smoke PROPERTIES
            INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        install(TARGETS coin_render_wayland_smoke
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    endif()
    if(TARGET coin_render_win32_smoke)
        install(TARGETS coin_render_win32_smoke
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    endif()
	if(TARGET coin_render_window_cone)
	set(_coin_render_window_targets coin_render_window_cone coin_render_viewer)
	if(TARGET coin_render_window_benchmark)
		list(APPEND _coin_render_window_targets coin_render_window_benchmark)
	endif()
	set_target_properties(${_coin_render_window_targets} PROPERTIES
		INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
	install(TARGETS ${_coin_render_window_targets}
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
	endif()
	if(TARGET coin_render_gl_benchmark)
		set_target_properties(coin_render_gl_benchmark coin_render_freecad_compare PROPERTIES
			INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
		install(TARGETS coin_render_gl_benchmark coin_render_freecad_compare
			RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
	endif()
	install(FILES AndroidManifest-p23.xml coin_render_android_smoke.cpp coin_render_appkit_smoke.mm coin_render_wayland_smoke.cpp coin_render_win32_smoke.cpp coin_render_window_cone.cpp coin_render_viewer.cpp coin_render_window_benchmark.cpp coin_render_gl_benchmark.cpp
		coin_render_freecad_compare.cpp CoinRenderBenchmarkAnimation.h freecad_export_iv.py generate_large_scene.py
		DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/coinrender COMPONENT development)
endif()
