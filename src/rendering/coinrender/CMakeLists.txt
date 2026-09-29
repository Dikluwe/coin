# CMakeLists.txt for CoinRender shared library

set(COIN_RENDER_SOURCES
	CoinRenderDiagnosticShell.cpp
	CoinRenderDepthPolicyElement.cpp
	CoinRenderFramePlan.cpp
	CoinRenderFramePlanBuilder.cpp
	CoinRenderFrameReuseCore.cpp
	${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxLowering.cpp
	CoinRenderImageCore.cpp
	CoinRenderIndexedGeometryCore.cpp
	CoinRenderManagerAdapter.cpp
	CoinRenderRecordingBackend.cpp
	CoinRenderCapabilities.cpp
	CoinRenderSceneManager.cpp
	CoinRenderCpuReferenceBackend.cpp
	CoinRenderRttExecution.cpp
	CoinRenderTarget.cpp
	${PROJECT_SOURCE_DIR}/src/actions/CoinRenderAction.cpp
)

if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
	list(APPEND COIN_RENDER_SOURCES ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuBackend.cpp)
elseif(COIN_RENDER_BACKEND STREQUAL "DAWN" OR COIN_RENDER_BACKEND STREQUAL "WGPU_NATIVE")
	list(APPEND COIN_RENDER_SOURCES ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuNativeBackend.cpp)
elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
	list(APPEND COIN_RENDER_SOURCES ${PROJECT_SOURCE_DIR}/src/actions/CoinBgfxAction.cpp)
	set(_coin_bgfx_shader_dir ${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/shaders)
	set(_coin_bgfx_generated_dir ${CMAKE_CURRENT_BINARY_DIR}/bgfx-generated)
	file(MAKE_DIRECTORY ${_coin_bgfx_generated_dir})
	foreach(_stage vs fs)
		if(_stage STREQUAL "vs")
			set(_shader_type vertex)
		else()
			set(_shader_type fragment)
		endif()
		foreach(_renderer spirv glsl)
			if(_renderer STREQUAL "spirv")
				set(_shader_profile spirv)
			else()
				set(_shader_profile 330)
			endif()
			set(_shader_output ${_coin_bgfx_generated_dir}/coin_bgfx_${_stage}_${_renderer}.h)
			add_custom_command(OUTPUT ${_shader_output}
				COMMAND ${COIN_BGFX_SHADERC_EXECUTABLE}
					-f ${_coin_bgfx_shader_dir}/${_stage}_base_color.sc
					-o ${_shader_output}
					--type ${_shader_type} --platform linux --profile ${_shader_profile}
					--varyingdef ${_coin_bgfx_shader_dir}/varying.def.sc
					--bin2c coin_bgfx_${_stage}_${_renderer}
					-i ${COIN_BGFX_SHADER_INCLUDE_DIR}
				DEPENDS ${_coin_bgfx_shader_dir}/${_stage}_base_color.sc
					${_coin_bgfx_shader_dir}/coin_depth.sh
					${_coin_bgfx_shader_dir}/coin_lighting.sh
					${_coin_bgfx_shader_dir}/coin_surface.sh
					${_coin_bgfx_shader_dir}/varying.def.sc
				VERBATIM)
			list(APPEND COIN_RENDER_SOURCES ${_shader_output})
		endforeach()
	endforeach()
	foreach(_shader peel_next composite weighted_oit weighted_composite depth_readback)
		foreach(_renderer spirv glsl)
			if(_renderer STREQUAL "spirv")
				set(_shader_profile spirv)
			else()
				set(_shader_profile 330)
			endif()
			set(_shader_output ${_coin_bgfx_generated_dir}/coin_bgfx_fs_${_shader}_${_renderer}.h)
			add_custom_command(OUTPUT ${_shader_output}
				COMMAND ${COIN_BGFX_SHADERC_EXECUTABLE}
					-f ${_coin_bgfx_shader_dir}/fs_${_shader}.sc
					-o ${_shader_output}
					--type fragment --platform linux --profile ${_shader_profile}
					--varyingdef ${_coin_bgfx_shader_dir}/varying.def.sc
					--bin2c coin_bgfx_fs_${_shader}_${_renderer}
					-i ${COIN_BGFX_SHADER_INCLUDE_DIR}
				DEPENDS ${_coin_bgfx_shader_dir}/fs_${_shader}.sc
					${_coin_bgfx_shader_dir}/coin_depth.sh
					${_coin_bgfx_shader_dir}/coin_lighting.sh
					${_coin_bgfx_shader_dir}/coin_surface.sh
					${_coin_bgfx_shader_dir}/varying.def.sc
				VERBATIM)
			list(APPEND COIN_RENDER_SOURCES ${_shader_output})
		endforeach()
	endforeach()
	list(APPEND COIN_RENDER_SOURCES ${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxBackend.cpp)
endif()

add_library(CoinRender SHARED ${COIN_RENDER_SOURCES})
add_library(CoinWgpuExperimental ALIAS CoinRender)

set_target_properties(CoinRender PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	COMPILE_DEFINITIONS "COIN_RENDER_INTERNAL;COIN_INTERNAL;HAVE_CONFIG_H"
)

target_include_directories(CoinRender PUBLIC
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/experimental/include>
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(CoinRender PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_BINARY_DIR}/src
	$<$<STREQUAL:${COIN_RENDER_BACKEND},BGFX>:${_coin_bgfx_generated_dir}>
)

target_link_libraries(CoinRender PRIVATE Coin)
if(COIN_RENDER_BACKEND STREQUAL "BGFX")
	target_link_libraries(CoinRender PUBLIC ${COIN_RENDER_LINK_LIBRARIES})
else()
	target_link_libraries(CoinRender PRIVATE ${COIN_RENDER_LINK_LIBRARIES})
endif()


if(COIN_INSTALL_RENDER_EXPERIMENTAL)
	set(COIN_RENDER_CONFIG_FIND_DEPENDENCIES "")
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		set(COIN_RENDER_CONFIG_FIND_DEPENDENCIES
			"find_dependency(bgfx CONFIG)")
	endif()
	include(CMakePackageConfigHelpers)
	set(_coin_render_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/CoinRender")
	configure_package_config_file(
		${CMAKE_CURRENT_SOURCE_DIR}/coin-render-config.cmake.in
		${CMAKE_CURRENT_BINARY_DIR}/CoinRenderConfig.cmake
		INSTALL_DESTINATION ${_coin_render_package_dir})
	write_basic_package_version_file(
		${CMAKE_CURRENT_BINARY_DIR}/CoinRenderConfigVersion.cmake
		VERSION ${PROJECT_VERSION}
		COMPATIBILITY SameMajorVersion)
	install(TARGETS CoinRender EXPORT CoinRenderTargets
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
	install(DIRECTORY ${PROJECT_SOURCE_DIR}/experimental/include/Inventor/
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Inventor
		COMPONENT development FILES_MATCHING PATTERN "*.h")
	install(FILES
		${CMAKE_CURRENT_BINARY_DIR}/CoinRenderConfig.cmake
		${CMAKE_CURRENT_BINARY_DIR}/CoinRenderConfigVersion.cmake
		DESTINATION ${_coin_render_package_dir} COMPONENT development)
	set(_coin_render_legacy_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/CoinWgpuExperimental")
	configure_package_config_file(
		${CMAKE_CURRENT_SOURCE_DIR}/coin-render-legacy-config.cmake.in
		${CMAKE_CURRENT_BINARY_DIR}/CoinWgpuExperimentalConfig.cmake
		INSTALL_DESTINATION ${_coin_render_legacy_package_dir})
	write_basic_package_version_file(
		${CMAKE_CURRENT_BINARY_DIR}/CoinWgpuExperimentalConfigVersion.cmake
		VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
	install(FILES
		${CMAKE_CURRENT_BINARY_DIR}/CoinWgpuExperimentalConfig.cmake
		${CMAKE_CURRENT_BINARY_DIR}/CoinWgpuExperimentalConfigVersion.cmake
		DESTINATION ${_coin_render_legacy_package_dir} COMPONENT development)
	install(EXPORT CoinRenderTargets
		FILE CoinRenderTargets.cmake
		NAMESPACE CoinRender::
		DESTINATION ${_coin_render_package_dir} COMPONENT development)
endif()
