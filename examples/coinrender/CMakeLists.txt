if(COIN_BUILD_RENDER_WINDOW_EXAMPLE)
  if(WIN32)
    if(NOT COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
      message(FATAL_ERROR "The P21 Win32 smoke requires COIN_RENDER_BACKEND=RUST_BRIDGE")
    endif()
    add_executable(coin_render_win32_smoke coin_render_win32_smoke.cpp)
    target_compile_definitions(coin_render_win32_smoke PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(coin_render_win32_smoke PRIVATE CoinRender Coin user32)
    target_include_directories(coin_render_win32_smoke PRIVATE
      ${PROJECT_SOURCE_DIR}/experimental/include
      ${PROJECT_SOURCE_DIR}/include
      ${PROJECT_BINARY_DIR}/include
      ${COIN_TARGET_INCLUDE_DIRECTORIES})
  else()
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
if(glfw3_FOUND AND X11_FOUND)
	target_compile_definitions(coin_render_window_cone PRIVATE HAVE_GLFW=1)
	target_link_libraries(coin_render_window_cone PRIVATE CoinRender glfw ${X11_LIBRARIES} Coin)
	target_include_directories(coin_render_window_cone PRIVATE ${X11_INCLUDE_DIR})
elseif(X11_FOUND)
	target_link_libraries(coin_render_window_cone PRIVATE CoinRender ${X11_LIBRARIES} Coin)
	target_include_directories(coin_render_window_cone PRIVATE ${X11_INCLUDE_DIR})
else()
	message(FATAL_ERROR "coin_render_window_cone on Linux requires X11.")
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
	install(FILES coin_render_win32_smoke.cpp coin_render_window_cone.cpp coin_render_viewer.cpp coin_render_window_benchmark.cpp coin_render_gl_benchmark.cpp
		coin_render_freecad_compare.cpp freecad_export_iv.py
		DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/coinrender COMPONENT development)
endif()
