add_executable(wgpu_window_cone wgpu_window_cone.cpp)

target_include_directories(wgpu_window_cone PRIVATE
	${PROJECT_SOURCE_DIR}/experimental/include
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)

find_package(glfw3 3.3 QUIET)
if(glfw3_FOUND AND X11_FOUND)
	target_compile_definitions(wgpu_window_cone PRIVATE HAVE_GLFW=1)
	target_link_libraries(wgpu_window_cone PRIVATE CoinWgpuExperimental glfw ${X11_LIBRARIES} Coin)
	target_include_directories(wgpu_window_cone PRIVATE ${X11_INCLUDE_DIR})
elseif(X11_FOUND)
	target_link_libraries(wgpu_window_cone PRIVATE CoinWgpuExperimental ${X11_LIBRARIES} Coin)
	target_include_directories(wgpu_window_cone PRIVATE ${X11_INCLUDE_DIR})
else()
	message(FATAL_ERROR "wgpu_window_cone on Linux requires X11.")
endif()

add_executable(wgpu_viewer wgpu_viewer.cpp)
target_link_libraries(wgpu_viewer PRIVATE CoinWgpuExperimental ${X11_LIBRARIES} Coin)
target_include_directories(wgpu_viewer PRIVATE
	${PROJECT_SOURCE_DIR}/experimental/include
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${X11_INCLUDE_DIR}
	${COIN_TARGET_INCLUDE_DIRECTORIES})

if(COIN_BUILD_LEGACY_GL_RENDERER)
	add_executable(wgpu_gl_benchmark wgpu_gl_benchmark.cpp)
	target_link_libraries(wgpu_gl_benchmark PRIVATE CoinWgpuExperimental Coin)
	target_include_directories(wgpu_gl_benchmark PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
endif()

if(COIN_INSTALL_WGPU_EXPERIMENTAL)
	set_target_properties(wgpu_window_cone wgpu_viewer PROPERTIES
		INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
	install(TARGETS wgpu_window_cone wgpu_viewer
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
	if(TARGET wgpu_gl_benchmark)
		set_target_properties(wgpu_gl_benchmark PROPERTIES
			INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
		install(TARGETS wgpu_gl_benchmark
			RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
	endif()
	install(FILES wgpu_window_cone.cpp wgpu_viewer.cpp wgpu_gl_benchmark.cpp
		DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/wgpu COMPONENT development)
endif()
