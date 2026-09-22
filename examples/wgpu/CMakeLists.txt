add_executable(wgpu_window_cone wgpu_window_cone.cpp)

target_include_directories(wgpu_window_cone PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)

find_package(glfw3 3.3 QUIET)
if(glfw3_FOUND AND X11_FOUND)
	target_compile_definitions(wgpu_window_cone PRIVATE HAVE_GLFW=1)
	target_link_libraries(wgpu_window_cone PRIVATE glfw ${X11_LIBRARIES} Coin)
	target_include_directories(wgpu_window_cone PRIVATE ${X11_INCLUDE_DIR})
elseif(X11_FOUND)
	target_link_libraries(wgpu_window_cone PRIVATE ${X11_LIBRARIES} Coin)
	target_include_directories(wgpu_window_cone PRIVATE ${X11_INCLUDE_DIR})
else()
	message(FATAL_ERROR "wgpu_window_cone on Linux requires X11.")
endif()
