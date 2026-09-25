# CMakeLists.txt for CoinWgpuExperimental shared library

set(COIN_WGPU_SOURCES
	SoWgpuDiagnosticShell.cpp
	SoWgpuFramePlan.cpp
	SoWgpuFramePlanBuilder.cpp
	SoWgpuFrameReuseCore.cpp
	SoWgpuBgfxCore.cpp
	SoWgpuImageCore.cpp
	SoWgpuIndexedGeometryCore.cpp
	SoWgpuRecordingBackend.cpp
	SoWgpuCapabilities.cpp
	SoWgpuSceneManager.cpp
	SoWgpuCpuReferenceBackend.cpp
	SoWgpuRenderTarget.cpp
	${PROJECT_SOURCE_DIR}/src/actions/SoWgpuRenderAction.cpp
)

if(COIN_WGPU_BACKEND STREQUAL "RUST_BRIDGE")
	list(APPEND COIN_WGPU_SOURCES SoWgpuFfiFrame.cpp SoWgpuRustBackend.cpp)
elseif(COIN_WGPU_BACKEND STREQUAL "DAWN" OR COIN_WGPU_BACKEND STREQUAL "WGPU_NATIVE")
	list(APPEND COIN_WGPU_SOURCES SoWgpuNativeBackend.cpp)
elseif(COIN_WGPU_BACKEND STREQUAL "BGFX")
	set(_coin_bgfx_shader_dir ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx)
	set(_coin_bgfx_generated_dir ${CMAKE_CURRENT_BINARY_DIR}/bgfx-generated)
	file(MAKE_DIRECTORY ${_coin_bgfx_generated_dir})
	foreach(_stage vs fs)
		if(_stage STREQUAL "vs")
			set(_shader_type vertex)
		else()
			set(_shader_type fragment)
		endif()
		set(_shader_output ${_coin_bgfx_generated_dir}/coin_bgfx_${_stage}_spirv.h)
		add_custom_command(OUTPUT ${_shader_output}
			COMMAND ${COIN_BGFX_SHADERC_EXECUTABLE}
				-f ${_coin_bgfx_shader_dir}/${_stage}_base_color.sc
				-o ${_shader_output}
				--type ${_shader_type} --platform linux --profile spirv
				--varyingdef ${_coin_bgfx_shader_dir}/varying.def.sc
				--bin2c coin_bgfx_${_stage}_spirv
				-i ${COIN_BGFX_SHADER_INCLUDE_DIR}
			DEPENDS ${_coin_bgfx_shader_dir}/${_stage}_base_color.sc
				${_coin_bgfx_shader_dir}/varying.def.sc
			VERBATIM)
		list(APPEND COIN_WGPU_SOURCES ${_shader_output})
	endforeach()
	list(APPEND COIN_WGPU_SOURCES SoWgpuBgfxBackend.cpp)
endif()

add_library(CoinWgpuExperimental SHARED ${COIN_WGPU_SOURCES})

set_target_properties(CoinWgpuExperimental PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	COMPILE_DEFINITIONS "COIN_WGPU_INTERNAL;COIN_INTERNAL;HAVE_CONFIG_H"
)

target_include_directories(CoinWgpuExperimental PUBLIC
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/experimental/include>
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(CoinWgpuExperimental PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_BINARY_DIR}/src
	$<$<STREQUAL:${COIN_WGPU_BACKEND},BGFX>:${_coin_bgfx_generated_dir}>
)

target_link_libraries(CoinWgpuExperimental PRIVATE
	Coin
	${COIN_WGPU_LINK_LIBRARIES}
)

if(COIN_INSTALL_WGPU_EXPERIMENTAL)
	include(CMakePackageConfigHelpers)
	set(_coin_wgpu_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/CoinWgpuExperimental")
	configure_package_config_file(
		${CMAKE_CURRENT_SOURCE_DIR}/coin-wgpu-experimental-config.cmake.in
		${CMAKE_CURRENT_BINARY_DIR}/CoinWgpuExperimentalConfig.cmake
		INSTALL_DESTINATION ${_coin_wgpu_package_dir})
	write_basic_package_version_file(
		${CMAKE_CURRENT_BINARY_DIR}/CoinWgpuExperimentalConfigVersion.cmake
		VERSION ${PROJECT_VERSION}
		COMPATIBILITY SameMajorVersion)
	install(TARGETS CoinWgpuExperimental EXPORT CoinWgpuExperimentalTargets
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
	install(DIRECTORY ${PROJECT_SOURCE_DIR}/experimental/include/Inventor/
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Inventor
		COMPONENT development FILES_MATCHING PATTERN "*.h")
	install(FILES
		${CMAKE_CURRENT_BINARY_DIR}/CoinWgpuExperimentalConfig.cmake
		${CMAKE_CURRENT_BINARY_DIR}/CoinWgpuExperimentalConfigVersion.cmake
		DESTINATION ${_coin_wgpu_package_dir} COMPONENT development)
	install(EXPORT CoinWgpuExperimentalTargets
		FILE CoinWgpuExperimentalTargets.cmake
		NAMESPACE CoinWgpuExperimental::
		DESTINATION ${_coin_wgpu_package_dir} COMPONENT development)
endif()
