# CoinRender-specific tests, kept separate from the general testsuite.
if(COIN_BUILD_RENDER)
	if((COIN_TEST_RENDER_STRICT_DISPLAY OR COIN_TEST_RENDER_GL_REFERENCE) AND
	   NOT COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		message(FATAL_ERROR "Strict WebGPU display/GL reference tests require COIN_RENDER_BACKEND=RUST_BRIDGE")
	endif()
	add_executable(CoinRenderActionTest
		../actions/CoinRenderActionTest.cpp
	)
	set_target_properties(CoinRenderActionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderActionTest CoinRender Coin)
	target_include_directories(CoinRenderActionTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderActionTest COMMAND CoinRenderActionTest)
	set_tests_properties(CoinRenderActionTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderBackendBoundaryTest CoinRenderBackendBoundaryTest.cpp)
	set_target_properties(CoinRenderBackendBoundaryTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderBackendBoundaryTest CoinRender Coin)
	target_include_directories(CoinRenderBackendBoundaryTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderBackendBoundaryTest COMMAND CoinRenderBackendBoundaryTest)
	set_tests_properties(CoinRenderBackendBoundaryTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderRuntimeBoundaryTest CoinRenderRuntimeBoundaryTest.cpp)
	set_target_properties(CoinRenderRuntimeBoundaryTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderRuntimeBoundaryTest CoinRender Coin)
	target_include_directories(CoinRenderRuntimeBoundaryTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderRuntimeBoundaryTest COMMAND CoinRenderRuntimeBoundaryTest)
	set_tests_properties(CoinRenderRuntimeBoundaryTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderPlanAssemblyCoreTest CoinRenderPlanAssemblyCoreTest.cpp)
	set_target_properties(CoinRenderPlanAssemblyCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderPlanAssemblyCoreTest CoinRender Coin)
	target_include_directories(CoinRenderPlanAssemblyCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderPlanAssemblyCoreTest COMMAND CoinRenderPlanAssemblyCoreTest)
	set_tests_properties(CoinRenderPlanAssemblyCoreTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderTransformCoreTest CoinRenderTransformCoreTest.cpp)
	set_target_properties(CoinRenderTransformCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderTransformCoreTest CoinRender Coin)
	target_include_directories(CoinRenderTransformCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderTransformCoreTest COMMAND CoinRenderTransformCoreTest)
	set_tests_properties(CoinRenderTransformCoreTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderCameraReuseReferenceTest CoinRenderCameraReuseReferenceTest.cpp)
	set_target_properties(CoinRenderCameraReuseReferenceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderCameraReuseReferenceTest CoinRender Coin)
	if(TARGET OpenGL::GL)
		target_link_libraries(CoinRenderCameraReuseReferenceTest OpenGL::GL)
	else()
		target_link_libraries(CoinRenderCameraReuseReferenceTest ${OPENGL_gl_LIBRARY})
	endif()
	target_include_directories(CoinRenderCameraReuseReferenceTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderCameraReuseReferenceTest COMMAND CoinRenderCameraReuseReferenceTest)
	set_tests_properties(CoinRenderCameraReuseReferenceTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

	add_executable(CoinRenderSamplingPolicyTest CoinRenderSamplingPolicyTest.cpp)
	set_target_properties(CoinRenderSamplingPolicyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderSamplingPolicyTest CoinRender Coin)
	target_include_directories(CoinRenderSamplingPolicyTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	if(UNIX AND NOT APPLE AND X11_FOUND)
		target_compile_definitions(CoinRenderSamplingPolicyTest PRIVATE COIN_SAMPLING_X11)
		target_link_libraries(CoinRenderSamplingPolicyTest ${X11_LIBRARIES})
		target_include_directories(CoinRenderSamplingPolicyTest PRIVATE ${X11_INCLUDE_DIR})
		add_test(NAME CoinRenderSamplingPolicyWindowTest COMMAND CoinRenderSamplingPolicyTest --window)
		set_tests_properties(CoinRenderSamplingPolicyWindowTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_SAMPLING_STUDY=fetch")
	endif()
	if(WIN32)
		target_compile_definitions(CoinRenderSamplingPolicyTest PRIVATE _WIN32_WINNT=0x0A00)
		target_link_libraries(CoinRenderSamplingPolicyTest user32 gdi32)
		add_test(NAME CoinRenderSamplingPolicyWindowTest COMMAND CoinRenderSamplingPolicyTest --window)
		set_tests_properties(CoinRenderSamplingPolicyWindowTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
	endif()
	add_test(NAME CoinRenderSamplingPolicyTest COMMAND CoinRenderSamplingPolicyTest)
	add_test(NAME CoinRenderSamplingPolicyGpuTest COMMAND CoinRenderSamplingPolicyTest --gpu)
	set_tests_properties(CoinRenderSamplingPolicyGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77
		ENVIRONMENT "COIN_SAMPLING_STUDY=fetch")

	add_executable(CoinRenderSelectionTest CoinRenderSelectionTest.cpp)
	set_target_properties(CoinRenderSelectionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderSelectionTest CoinRender Coin)
	target_include_directories(CoinRenderSelectionTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderSelectionTest COMMAND CoinRenderSelectionTest)
	set_tests_properties(CoinRenderSelectionTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderSelection_${renderer} COMMAND CoinRenderSelectionTest)
			set_tests_properties(CoinRenderSelection_${renderer} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
		endforeach()
	endif()

	add_executable(CoinRenderProductTest CoinRenderProductTest.cpp)
	set_target_properties(CoinRenderProductTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderProductTest CoinRender Coin)
	target_include_directories(CoinRenderProductTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderProductTest COMMAND CoinRenderProductTest)
	set_tests_properties(CoinRenderProductTest PROPERTIES TIMEOUT 30)

	# The same Coin-level contract runs against Rust/wgpu, BGFX and Recording
	# in separate builds. Backend-specific behavior remains in separate tests.
	add_executable(CoinRenderBackendContractTest CoinRenderBackendContractTest.cpp)
	set_target_properties(CoinRenderBackendContractTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderBackendContractTest CoinRender Coin)
	target_include_directories(CoinRenderBackendContractTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderBackendContractTest COMMAND CoinRenderBackendContractTest)
	set_tests_properties(CoinRenderBackendContractTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

	add_executable(CoinRenderOffscreenTest
		CoinRenderOffscreenTest.cpp
	)
	set_target_properties(CoinRenderOffscreenTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderOffscreenTest CoinRender Coin)
	target_include_directories(CoinRenderOffscreenTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderOffscreenTest COMMAND CoinRenderOffscreenTest)
	set_tests_properties(CoinRenderOffscreenTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderGateG0Test
		CoinRenderGateG0Test.cpp
	)
	set_target_properties(CoinRenderGateG0Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderGateG0Test CoinRender Coin)
	target_include_directories(CoinRenderGateG0Test PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderGateG0Test COMMAND CoinRenderGateG0Test)
	set_tests_properties(CoinRenderGateG0Test PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderFrameCoreTest CoinRenderFrameCoreTest.cpp)
	set_target_properties(CoinRenderFrameCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderFrameCoreTest CoinRender Coin)
	target_include_directories(CoinRenderFrameCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderFrameCoreTest COMMAND CoinRenderFrameCoreTest)
	set_tests_properties(CoinRenderFrameCoreTest PROPERTIES TIMEOUT 30)

	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
	add_executable(CoinBgfxReadbackModesTest ../coinbgfx/CoinBgfxReadbackModesTest.cpp)
	set_target_properties(CoinBgfxReadbackModesTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxReadbackModesTest CoinRender Coin)
	target_include_directories(CoinBgfxReadbackModesTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(renderer ${_coin_bgfx_test_renderers})
		add_test(NAME CoinBgfxReadbackModes_${renderer} COMMAND ${CMAKE_COMMAND} -E env COIN_BGFX_RENDERER=${renderer} COIN_BGFX_TRANSPARENCY=auto $<TARGET_FILE:CoinBgfxReadbackModesTest>)
		set_tests_properties(CoinBgfxReadbackModes_${renderer} PROPERTIES TIMEOUT 90)
	endforeach()
	add_executable(CoinBgfxSurfaceFeaturesTest ../coinbgfx/CoinBgfxSurfaceFeaturesTest.cpp)
	set_target_properties(CoinBgfxSurfaceFeaturesTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxSurfaceFeaturesTest CoinRender Coin)
	target_include_directories(CoinBgfxSurfaceFeaturesTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(renderer ${_coin_bgfx_test_renderers})
		foreach(mode object weighted_oit sorted_layers)
			add_test(NAME CoinBgfxSurface_${renderer}_${mode} COMMAND ${CMAKE_COMMAND} -E env COIN_BGFX_RENDERER=${renderer} COIN_BGFX_TRANSPARENCY=${mode} $<TARGET_FILE:CoinBgfxSurfaceFeaturesTest>)
			set_tests_properties(CoinBgfxSurface_${renderer}_${mode} PROPERTIES TIMEOUT 90)
		endforeach()
	endforeach()
	endif()
	add_executable(CoinBgfxCoreTest ../coinbgfx/CoinBgfxCoreTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxLowering.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxProgramCache.cpp)
	set_target_properties(CoinBgfxCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxCoreTest CoinRender Coin)
	target_include_directories(CoinBgfxCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinBgfxCoreTest COMMAND CoinBgfxCoreTest)
	add_executable(CoinBgfxProgramSelectionTest ../coinbgfx/CoinBgfxProgramSelectionTest.cpp)
	target_sources(CoinBgfxProgramSelectionTest PRIVATE
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxProgramSelection.cpp)
	set_target_properties(CoinBgfxProgramSelectionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxProgramSelectionTest CoinRender Coin)
	target_include_directories(CoinBgfxProgramSelectionTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src)
	add_test(NAME CoinBgfxProgramSelectionTest COMMAND CoinBgfxProgramSelectionTest)
	set_tests_properties(CoinBgfxProgramSelectionTest PROPERTIES TIMEOUT 30)
	add_executable(CoinRenderDepthContractTest CoinRenderDepthContractTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxLowering.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp)
	target_sources(CoinRenderDepthContractTest PRIVATE ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp)
	target_sources(CoinRenderDepthContractTest PRIVATE
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
	set_target_properties(CoinRenderDepthContractTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderDepthContractTest CoinRender Coin)
	target_include_directories(CoinRenderDepthContractTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src)
	add_test(NAME CoinRenderDepthContractTest COMMAND CoinRenderDepthContractTest)
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderDepthContractGpuTest COMMAND CoinRenderDepthContractTest --gpu)
		set_tests_properties(CoinRenderDepthContractGpuTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
	endif()
	set_tests_properties(CoinBgfxCoreTest PROPERTIES TIMEOUT 30)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		add_test(NAME CoinRenderDepthContractVulkanTest COMMAND CoinRenderDepthContractTest --gpu)
		add_test(NAME CoinRenderDepthContractOpenGLTest COMMAND CoinRenderDepthContractTest --gpu)
		set_tests_properties(CoinRenderDepthContractVulkanTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=vulkan;COIN_BGFX_DISABLE_DRAW_GROUPING=1")
		set_tests_properties(CoinRenderDepthContractOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl;COIN_BGFX_DISABLE_DRAW_GROUPING=1")
		add_test(NAME CoinRenderProductOpenGLTest COMMAND CoinRenderProductTest)
		set_tests_properties(CoinRenderProductOpenGLTest PROPERTIES TIMEOUT 30
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_executable(CoinBgfxOffscreenTest ../coinbgfx/CoinBgfxOffscreenTest.cpp)
		set_target_properties(CoinBgfxOffscreenTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinBgfxOffscreenTest CoinRender Coin)
		target_include_directories(CoinBgfxOffscreenTest PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinBgfxOffscreenTest COMMAND CoinBgfxOffscreenTest)
		set_tests_properties(CoinBgfxOffscreenTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
		add_test(NAME CoinBgfxOpenGLTest COMMAND CoinBgfxOffscreenTest)
		set_tests_properties(CoinBgfxOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_executable(CoinBgfxInstancingTest ../coinbgfx/CoinBgfxInstancingTest.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxLowering.cpp)
		set_target_properties(CoinBgfxInstancingTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinBgfxInstancingTest CoinRender Coin)
		target_include_directories(CoinBgfxInstancingTest PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinBgfxInstancingTest COMMAND CoinBgfxInstancingTest)
		set_tests_properties(CoinBgfxInstancingTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=vulkan")
		add_test(NAME CoinBgfxInstancingOpenGLTest COMMAND CoinBgfxInstancingTest)
		set_tests_properties(CoinBgfxInstancingOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_test(NAME CoinRenderBackendContractOpenGLTest COMMAND CoinRenderBackendContractTest)
		set_tests_properties(CoinRenderBackendContractOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_executable(CoinBgfxTransparencyTest ../coinbgfx/CoinBgfxTransparencyTest.cpp)
		set_target_properties(CoinBgfxTransparencyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinBgfxTransparencyTest CoinRender Coin)
		target_include_directories(CoinBgfxTransparencyTest PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinBgfxTransparencyTest COMMAND CoinBgfxTransparencyTest)
		set_tests_properties(CoinBgfxTransparencyTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
		add_test(NAME CoinBgfxTransparencyOpenGLTest COMMAND CoinBgfxTransparencyTest)
		set_tests_properties(CoinBgfxTransparencyOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_test(NAME CoinBgfxSortedLayersTest COMMAND CoinBgfxTransparencyTest)
		set_tests_properties(CoinBgfxSortedLayersTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_TRANSPARENCY=sorted_layers")
		add_test(NAME CoinBgfxSortedLayersOpenGLTest COMMAND CoinBgfxTransparencyTest)
		set_tests_properties(CoinBgfxSortedLayersOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_TRANSPARENCY=sorted_layers;COIN_BGFX_RENDERER=opengl")
		add_test(NAME CoinBgfxWeightedOitTest COMMAND CoinBgfxTransparencyTest)
		set_tests_properties(CoinBgfxWeightedOitTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_TRANSPARENCY=weighted_oit")
		add_test(NAME CoinBgfxWeightedOitOpenGLTest COMMAND CoinBgfxTransparencyTest)
		set_tests_properties(CoinBgfxWeightedOitOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_TRANSPARENCY=weighted_oit;COIN_BGFX_RENDERER=opengl")
		if(X11_FOUND)
			add_executable(CoinBgfxWindowTest ../coinbgfx/CoinBgfxWindowTest.cpp)
			set_target_properties(CoinBgfxWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(CoinBgfxWindowTest CoinRender Coin ${X11_LIBRARIES})
			target_include_directories(CoinBgfxWindowTest PRIVATE
				${PROJECT_SOURCE_DIR}/include
				${PROJECT_SOURCE_DIR}/src
				${PROJECT_BINARY_DIR}/include
				${PROJECT_BINARY_DIR}/src
				${X11_INCLUDE_DIR}
				${COIN_TARGET_INCLUDE_DIRECTORIES}
			)
			add_test(NAME CoinBgfxWindowTest COMMAND CoinBgfxWindowTest)
			set_tests_properties(CoinBgfxWindowTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
			add_test(NAME CoinBgfxWindowOpenGLTest COMMAND CoinBgfxWindowTest)
			set_tests_properties(CoinBgfxWindowOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
			add_test(NAME CoinBgfxRenderManagerAdapterTest COMMAND CoinBgfxWindowTest)
			set_tests_properties(CoinBgfxRenderManagerAdapterTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_TEST_RENDER_MANAGER_ADAPTER=1")
			add_test(NAME CoinBgfxRenderManagerAdapterOpenGLTest COMMAND CoinBgfxWindowTest)
			set_tests_properties(CoinBgfxRenderManagerAdapterOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_TEST_RENDER_MANAGER_ADAPTER=1;COIN_BGFX_RENDERER=opengl")
			add_executable(CoinBgfxMultiWindowTest ../coinbgfx/CoinBgfxMultiWindowTest.cpp)
			set_target_properties(CoinBgfxMultiWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(CoinBgfxMultiWindowTest CoinRender Coin ${X11_LIBRARIES})
			get_target_property(_bgfx_window_includes CoinBgfxWindowTest INCLUDE_DIRECTORIES)
			target_include_directories(CoinBgfxMultiWindowTest PRIVATE ${_bgfx_window_includes})
			add_test(NAME CoinBgfxMultiWindowTest COMMAND CoinBgfxMultiWindowTest)
			set_tests_properties(CoinBgfxMultiWindowTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
			add_test(NAME CoinBgfxMultiWindowOpenGLTest COMMAND CoinBgfxMultiWindowTest)
			set_tests_properties(CoinBgfxMultiWindowOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
			add_test(NAME CoinBgfxMultiWindowOffscreenFirstTest COMMAND CoinBgfxMultiWindowTest)
			set_tests_properties(CoinBgfxMultiWindowOffscreenFirstTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_TEST_OFFSCREEN_FIRST=1")
			add_test(NAME CoinBgfxMultiWindowOffscreenFirstOpenGLTest COMMAND CoinBgfxMultiWindowTest)
			set_tests_properties(CoinBgfxMultiWindowOffscreenFirstOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_TEST_OFFSCREEN_FIRST=1;COIN_BGFX_RENDERER=opengl")
			foreach(_composition weighted_oit sorted_layers)
				foreach(_renderer ${_coin_bgfx_test_renderers})
					set(_multi_composition_test "CoinBgfxMultiWindow_${_composition}_${_renderer}Test")
					add_test(NAME ${_multi_composition_test} COMMAND CoinBgfxMultiWindowTest)
					set_tests_properties(${_multi_composition_test} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
						ENVIRONMENT "COIN_BGFX_TRANSPARENCY=${_composition};COIN_BGFX_RENDERER=${_renderer}")
				endforeach()
			endforeach()
		endif()
	endif()

	add_executable(CoinRenderFrameReuseCoreTest CoinRenderFrameReuseCoreTest.cpp)
	set_target_properties(CoinRenderFrameReuseCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderFrameReuseCoreTest CoinRender Coin)
	target_include_directories(CoinRenderFrameReuseCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderFrameReuseCoreTest COMMAND CoinRenderFrameReuseCoreTest)
	set_tests_properties(CoinRenderFrameReuseCoreTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderDiagnosticShellTest CoinRenderDiagnosticShellTest.cpp)
	set_target_properties(CoinRenderDiagnosticShellTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderDiagnosticShellTest CoinRender Coin)
	target_include_directories(CoinRenderDiagnosticShellTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderDiagnosticShellTest COMMAND CoinRenderDiagnosticShellTest)
	set_tests_properties(CoinRenderDiagnosticShellTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderIndexedGeometryCoreTest CoinRenderIndexedGeometryCoreTest.cpp)
	set_target_properties(CoinRenderIndexedGeometryCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderIndexedGeometryCoreTest CoinRender Coin)
	target_include_directories(CoinRenderIndexedGeometryCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderIndexedGeometryCoreTest COMMAND CoinRenderIndexedGeometryCoreTest)
	set_tests_properties(CoinRenderIndexedGeometryCoreTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderGateG1Test
		CoinRenderGateG1Test.cpp
	)
	set_target_properties(CoinRenderGateG1Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderGateG1Test CoinRender Coin)
	target_include_directories(CoinRenderGateG1Test PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderGateG1Test COMMAND CoinRenderGateG1Test)
	set_tests_properties(CoinRenderGateG1Test PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderGateG2Test
		CoinRenderGateG2Test.cpp
	)
	set_target_properties(CoinRenderGateG2Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderGateG2Test CoinRender Coin)
	target_include_directories(CoinRenderGateG2Test PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderGateG2Test COMMAND CoinRenderGateG2Test)
	set_tests_properties(CoinRenderGateG2Test PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderIndexedFastPathTest
		CoinRenderIndexedFastPathTest.cpp
	)
	set_target_properties(CoinRenderIndexedFastPathTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderIndexedFastPathTest CoinRender Coin)
	target_include_directories(CoinRenderIndexedFastPathTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderIndexedFastPathTest COMMAND CoinRenderIndexedFastPathTest)
	set_tests_properties(CoinRenderIndexedFastPathTest PROPERTIES TIMEOUT 30)

	add_executable(CoinWgpuCacheTest
		../coinwgpu/CoinWgpuCacheTest.cpp
	)
	set_target_properties(CoinWgpuCacheTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinWgpuCacheTest CoinRender Coin)
	target_include_directories(CoinWgpuCacheTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinWgpuCacheTest COMMAND CoinWgpuCacheTest)
	set_tests_properties(CoinWgpuCacheTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderStabilizationTest
		CoinRenderStabilizationTest.cpp
	)
	set_target_properties(CoinRenderStabilizationTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderStabilizationTest CoinRender Coin)
	target_include_directories(CoinRenderStabilizationTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderStabilizationTest COMMAND CoinRenderStabilizationTest)
	set_tests_properties(CoinRenderStabilizationTest PROPERTIES TIMEOUT 30)

	# Deterministic benchmark workloads, including isolation of shared scene nodes.
	add_executable(CoinRenderBenchmarkAnimationTest
		CoinRenderBenchmarkAnimationTest.cpp)
	target_link_libraries(CoinRenderBenchmarkAnimationTest Coin)
	target_include_directories(CoinRenderBenchmarkAnimationTest PRIVATE
		${PROJECT_SOURCE_DIR}/examples/coinrender
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderBenchmarkAnimationTest COMMAND CoinRenderBenchmarkAnimationTest)
	set_tests_properties(CoinRenderBenchmarkAnimationTest PROPERTIES TIMEOUT 30)

	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
	add_executable(CoinRenderMaterialTest
		CoinRenderMaterialTest.cpp
	)
	set_target_properties(CoinRenderMaterialTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderMaterialTest CoinRender Coin)
	target_include_directories(CoinRenderMaterialTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderMaterialTest COMMAND CoinRenderMaterialTest)
	set_tests_properties(CoinRenderMaterialTest PROPERTIES TIMEOUT 30)
	endif()

	add_executable(CoinRenderTextureTest
		CoinRenderTextureTest.cpp
	)
	set_target_properties(CoinRenderTextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderTextureTest CoinRender Coin)
	target_include_directories(CoinRenderTextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderTextureTest COMMAND CoinRenderTextureTest)
	set_tests_properties(CoinRenderTextureTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderTextureSamplingTest CoinRenderTextureSamplingTest.cpp)
	if(WIN32 OR APPLE OR (UNIX AND NOT ANDROID))
		add_executable(CoinRenderGluNativeOracle CoinRenderGluNativeOracle.cpp)
		target_compile_definitions(CoinRenderGluNativeOracle PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_include_directories(CoinRenderGluNativeOracle PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src)
		target_link_libraries(CoinRenderGluNativeOracle PRIVATE CoinRender Coin OpenGL::GL)
		if(TARGET OpenGL::GLU)
			target_link_libraries(CoinRenderGluNativeOracle PRIVATE OpenGL::GLU)
		else()
			target_link_libraries(CoinRenderGluNativeOracle PRIVATE ${OPENGL_glu_LIBRARY})
		endif()
		if(WIN32)
			target_link_libraries(CoinRenderGluNativeOracle PRIVATE Coin gdi32 user32)
		elseif(NOT APPLE)
			find_package(GLUT REQUIRED)
			target_link_libraries(CoinRenderGluNativeOracle PRIVATE GLUT::GLUT)
		endif()
		add_test(NAME CoinRenderGluNativeOracleTest COMMAND CoinRenderGluNativeOracle)
		set_tests_properties(CoinRenderGluNativeOracleTest PROPERTIES TIMEOUT 20)
	endif()
	target_compile_definitions(CoinRenderTextureSamplingTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderTextureSamplingTest CoinRender Coin)
	target_include_directories(CoinRenderTextureSamplingTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderTextureSamplingCaptureTest COMMAND CoinRenderTextureSamplingTest)
	if(NOT SIMAGE_RUNTIME_LINKING AND NOT HAVE_LIBSIMAGE)
		add_test(NAME CoinRenderTextureSamplingGluFallbackTest
			COMMAND CoinRenderTextureSamplingTest --scale-policy-pot-glu-probe)
		set_tests_properties(CoinRenderTextureSamplingGluFallbackTest PROPERTIES
			TIMEOUT 120
			ENVIRONMENT "COIN_GLGLUE_DISABLE_NON_POWER_OF_TWO_TEXTURES=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1;COIN_GLXGLUE_NO_PBUFFERS=1")
	endif()
	add_test(NAME CoinRenderTextureSamplingConfigLimitTest COMMAND CoinRenderTextureSamplingTest --reject-config)
	set_tests_properties(CoinRenderTextureSamplingConfigLimitTest PROPERTIES TIMEOUT 60 ENVIRONMENT "COIN_TEX2_LINEAR_LIMIT=0.4")
	set_tests_properties(CoinRenderTextureSamplingCaptureTest PROPERTIES TIMEOUT 120)
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderTextureSamplingGpuTest COMMAND CoinRenderTextureSamplingTest --gpu)
		set_tests_properties(CoinRenderTextureSamplingGpuTest PROPERTIES TIMEOUT 180)
	elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderTextureSampling_${renderer} COMMAND CoinRenderTextureSamplingTest --gpu)
			set_tests_properties(CoinRenderTextureSampling_${renderer} PROPERTIES TIMEOUT 180
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
		endforeach()
	endif()

	add_executable(CoinRenderAdvancedTextureTest CoinRenderAdvancedTextureTest.cpp)
	target_compile_definitions(CoinRenderAdvancedTextureTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderAdvancedTextureTest CoinRender Coin)
	target_include_directories(CoinRenderAdvancedTextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderAdvancedTextureCoreTest COMMAND CoinRenderAdvancedTextureTest)
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderAdvancedTextureGpuTest COMMAND CoinRenderAdvancedTextureTest --gpu)
	elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderAdvancedTexture_${renderer} COMMAND CoinRenderAdvancedTextureTest --gpu)
			set_tests_properties(CoinRenderAdvancedTexture_${renderer} PROPERTIES ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
		endforeach()
	endif()

	if(COIN_BUILD_LEGACY_GL_RENDERER)
	add_executable(CoinRenderRttProfileTest CoinRenderRttProfileTest.cpp)
	target_compile_definitions(CoinRenderRttProfileTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderRttProfileTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderRttProfileTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderRttProfileStagedTest COMMAND CoinRenderRttProfileTest)
		add_test(NAME CoinRenderRttProfileDirectTest COMMAND CoinRenderRttProfileTest --direct)
		add_test(NAME CoinRenderRttProfilePbufferTest COMMAND CoinRenderRttProfileTest)
		add_test(NAME CoinRenderRttProfileMipsDirectTest COMMAND CoinRenderRttProfileTest --mips-direct)
		add_test(NAME CoinRenderRttProfileMipsTest COMMAND CoinRenderRttProfileTest --mips)
		add_test(NAME CoinRenderRttProfileMipsPbufferTest COMMAND CoinRenderRttProfileTest --mips)
		set_tests_properties(CoinRenderRttProfileMipsDirectTest CoinRenderRttProfileMipsTest
			CoinRenderRttProfileMipsPbufferTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
		set_tests_properties(CoinRenderRttProfileMipsPbufferTest PROPERTIES ENVIRONMENT "COIN_DONT_USE_FBO=1")
		set_tests_properties(CoinRenderRttProfileStagedTest CoinRenderRttProfileDirectTest
			CoinRenderRttProfilePbufferTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
		set_tests_properties(CoinRenderRttProfilePbufferTest PROPERTIES ENVIRONMENT "COIN_DONT_USE_FBO=1")
	elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			foreach(route Staged Direct Mips MipsDirect MipsPbuffer)
				set(_rtt_profile_args)
				if(route STREQUAL "Direct")
					set(_rtt_profile_args --direct)
				endif()
				if(route STREQUAL "MipsDirect")
					set(_rtt_profile_args --mips-direct)
				elseif(route MATCHES "^Mips")
					set(_rtt_profile_args --mips)
				endif()
				add_test(NAME CoinRenderRttProfile${route}_${renderer} COMMAND CoinRenderRttProfileTest ${_rtt_profile_args})
				set_tests_properties(CoinRenderRttProfile${route}_${renderer} PROPERTIES TIMEOUT 180
					SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
				if(route STREQUAL "MipsPbuffer")
					set_property(TEST CoinRenderRttProfile${route}_${renderer} APPEND PROPERTY ENVIRONMENT "COIN_DONT_USE_FBO=1")
				endif()
			endforeach()
		endforeach()
	endif()
	add_executable(CoinRenderRttMatrixTest CoinRenderRttMatrixTest.cpp)
	target_compile_definitions(CoinRenderRttMatrixTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderRttMatrixTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderRttMatrixTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderRttMatrixCaptureTest COMMAND CoinRenderRttMatrixTest)
	add_test(NAME CoinRenderRttViewportCaptureTest COMMAND CoinRenderRttMatrixTest "" --external)
	add_test(NAME CoinRenderRttViewportFboTest COMMAND CoinRenderRttMatrixTest --gl --external)
	add_test(NAME CoinRenderRttViewportPbufferTest COMMAND CoinRenderRttMatrixTest --gl --external)
	set_tests_properties(CoinRenderRttViewportCaptureTest CoinRenderRttViewportFboTest CoinRenderRttViewportPbufferTest PROPERTIES TIMEOUT 60)
	set_tests_properties(CoinRenderRttViewportFboTest PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_DONT_USE_FBO=0")
	set_tests_properties(CoinRenderRttViewportPbufferTest PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_DONT_USE_FBO=1")
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE" OR COIN_RENDER_BACKEND STREQUAL "BGFX")
		add_test(NAME CoinRenderRttMatrixGpuTest COMMAND CoinRenderRttMatrixTest --gpu)
		add_test(NAME CoinRenderRttMatrixDirectTest COMMAND CoinRenderRttMatrixTest --direct)
		add_test(NAME CoinRenderRttViewportGpuTest COMMAND CoinRenderRttMatrixTest --gpu --external)
		add_test(NAME CoinRenderRttViewportDirectTest COMMAND CoinRenderRttMatrixTest --direct --external)
		set_tests_properties(CoinRenderRttViewportGpuTest CoinRenderRttViewportDirectTest PROPERTIES TIMEOUT 60)
		set_tests_properties(CoinRenderRttMatrixGpuTest CoinRenderRttMatrixDirectTest PROPERTIES TIMEOUT 60)
	endif()
	add_test(NAME CoinRenderRttMatrixFboTest COMMAND CoinRenderRttMatrixTest --gl)
	add_test(NAME CoinRenderRttMatrixPbufferTest COMMAND CoinRenderRttMatrixTest --gl)
	set_tests_properties(CoinRenderRttMatrixCaptureTest
		CoinRenderRttMatrixFboTest CoinRenderRttMatrixPbufferTest PROPERTIES TIMEOUT 60)
	set_tests_properties(CoinRenderRttMatrixFboTest PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_DONT_USE_FBO=0")
	set_tests_properties(CoinRenderRttMatrixPbufferTest PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_DONT_USE_FBO=1")
	endif()

	add_executable(CoinRenderGeometryViewportTest CoinRenderGeometryViewportTest.cpp)
	target_compile_definitions(CoinRenderGeometryViewportTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderGeometryViewportTest CoinRender Coin)
	target_include_directories(CoinRenderGeometryViewportTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderGeometryViewportCaptureTest COMMAND CoinRenderGeometryViewportTest)
	set_tests_properties(CoinRenderGeometryViewportCaptureTest PROPERTIES TIMEOUT 120)
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderGeometryViewportGpuTest COMMAND CoinRenderGeometryViewportTest --gpu)
		set_tests_properties(CoinRenderGeometryViewportGpuTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
	elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderGeometryViewport_${renderer} COMMAND CoinRenderGeometryViewportTest --gpu)
			set_tests_properties(CoinRenderGeometryViewport_${renderer} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
		endforeach()
	endif()

	# Manual study tool: reports differences without changing qualification gates.
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		add_executable(CoinRenderRasterStudy CoinRenderRasterStudy.cpp)
		target_compile_definitions(CoinRenderRasterStudy PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderRasterStudy CoinRender Coin OpenGL::GL)
		target_include_directories(CoinRenderRasterStudy PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	endif()

	add_executable(CoinRenderProceduralTextureTest CoinRenderProceduralTextureTest.cpp)
	target_compile_definitions(CoinRenderProceduralTextureTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderProceduralTextureTest CoinRender Coin)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		target_link_libraries(CoinRenderProceduralTextureTest OpenGL::GL)
	endif()
	target_include_directories(CoinRenderProceduralTextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderProceduralTextureCaptureTest COMMAND CoinRenderProceduralTextureTest)
	set_tests_properties(CoinRenderProceduralTextureCaptureTest PROPERTIES TIMEOUT 60)
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderProceduralTextureGpuTest COMMAND CoinRenderProceduralTextureTest --gpu)
		set_tests_properties(CoinRenderProceduralTextureGpuTest PROPERTIES TIMEOUT 120)
	elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderProceduralTexture_${renderer} COMMAND CoinRenderProceduralTextureTest --gpu)
			set_tests_properties(CoinRenderProceduralTexture_${renderer} PROPERTIES TIMEOUT 120
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
		endforeach()
	endif()

	add_executable(CoinRenderLightingTest
		CoinRenderLightingTest.cpp
	)
	set_target_properties(CoinRenderLightingTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderLightingTest CoinRender Coin)
	target_include_directories(CoinRenderLightingTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderLightingTest COMMAND CoinRenderLightingTest)
	set_tests_properties(CoinRenderLightingTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderGouraudInterpolationTest
		CoinRenderGouraudInterpolationTest.cpp
	)
	set_target_properties(CoinRenderGouraudInterpolationTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderGouraudInterpolationTest CoinRender Coin)
	target_include_directories(CoinRenderGouraudInterpolationTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderGouraudInterpolationTest COMMAND CoinRenderGouraudInterpolationTest)
	set_tests_properties(CoinRenderGouraudInterpolationTest PROPERTIES TIMEOUT 30)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(_gouraud_renderer ${_coin_bgfx_test_renderers})
			foreach(_gouraud_mode object weighted_oit sorted_layers)
				add_test(NAME CoinRenderGouraud_${_gouraud_renderer}_${_gouraud_mode}
					COMMAND CoinRenderGouraudInterpolationTest)
				set_tests_properties(CoinRenderGouraud_${_gouraud_renderer}_${_gouraud_mode}
					PROPERTIES TIMEOUT 30 ENVIRONMENT
					"COIN_BGFX_RENDERER=${_gouraud_renderer};COIN_BGFX_TRANSPARENCY=${_gouraud_mode}")
			endforeach()
		endforeach()
	endif()

	add_executable(CoinRenderFogTest
		CoinRenderFogTest.cpp
	)
	set_target_properties(CoinRenderFogTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderFogTest CoinRender Coin)
	target_include_directories(CoinRenderFogTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderFogTest COMMAND CoinRenderFogTest)
	set_tests_properties(CoinRenderFogTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderClipPlaneTest CoinRenderClipPlaneTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp)
	set_target_properties(CoinRenderClipPlaneTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderClipPlaneTest CoinRender Coin)
	target_include_directories(CoinRenderClipPlaneTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderClipPlaneTest COMMAND CoinRenderClipPlaneTest)
	set_tests_properties(CoinRenderClipPlaneTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		set_tests_properties(CoinRenderClipPlaneTest PROPERTIES ENVIRONMENT "COIN_BGFX_RENDERER=vulkan;COIN_BGFX_TRANSPARENCY=object")
		foreach(renderer ${_coin_bgfx_test_renderers})
			foreach(mode object weighted_oit sorted_layers)
				add_test(NAME CoinRenderClipPlane_${renderer}_${mode} COMMAND CoinRenderClipPlaneTest)
				set_tests_properties(CoinRenderClipPlane_${renderer}_${mode} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=${mode}")
			endforeach()
		endforeach()
	endif()


	add_executable(CoinRenderDrawStyleTest CoinRenderDrawStyleTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp)
	set_target_properties(CoinRenderDrawStyleTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderDrawStyleTest CoinRender Coin)
	target_include_directories(CoinRenderDrawStyleTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderDrawStyleTest COMMAND CoinRenderDrawStyleTest)
	set(_coin_draw_style_timeout 60)
	if(WIN32)
		# The full CPU/GPU stipple matrix performs over 15,000 captures.
		# Windows wgpu/OpenGL can exceed 120s on the first run; a repeat
		# with mandatory GL references took 104s on GTX 1060. Keep the full
		# matrix and allow startup/scheduling variation within a finite limit.
		set(_coin_draw_style_timeout 240)
	endif()
	set_tests_properties(CoinRenderDrawStyleTest PROPERTIES TIMEOUT ${_coin_draw_style_timeout} SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		set_tests_properties(CoinRenderDrawStyleTest PROPERTIES ENVIRONMENT "COIN_BGFX_RENDERER=vulkan;COIN_BGFX_TRANSPARENCY=object")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderDrawStyle_${renderer} COMMAND CoinRenderDrawStyleTest)
			set_tests_properties(CoinRenderDrawStyle_${renderer} PROPERTIES TIMEOUT ${_coin_draw_style_timeout} SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
		endforeach()
	endif()
	unset(_coin_draw_style_timeout)


	add_executable(CoinRenderMultitextureTest CoinRenderMultitextureTest.cpp)
	set_target_properties(CoinRenderMultitextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderMultitextureTest CoinRender Coin)
	target_include_directories(CoinRenderMultitextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderMultitextureTest COMMAND CoinRenderMultitextureTest)
	set_tests_properties(CoinRenderMultitextureTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderMultitexture_${renderer} COMMAND CoinRenderMultitextureTest)
			set_tests_properties(CoinRenderMultitexture_${renderer} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
		endforeach()
	endif()

	add_executable(CoinRenderPeelingTest CoinRenderPeelingTest.cpp)
	set_target_properties(CoinRenderPeelingTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderPeelingTest CoinRender Coin)
	target_include_directories(CoinRenderPeelingTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderPeelingTest COMMAND CoinRenderPeelingTest)
	set_tests_properties(CoinRenderPeelingTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderPeeling_${renderer} COMMAND CoinRenderPeelingTest)
			add_test(NAME CoinRenderOit_${renderer} COMMAND CoinRenderPeelingTest --weighted)
			set_tests_properties(CoinRenderOit_${renderer} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=weighted_oit")
			set_tests_properties(CoinRenderPeeling_${renderer} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
		endforeach()
	endif()

	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderWeightedOitTest COMMAND CoinRenderPeelingTest --weighted)
		set_tests_properties(CoinRenderWeightedOitTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
	endif()

	add_executable(CoinRenderTransparencyTest CoinRenderTransparencyTest.cpp)
	set_target_properties(CoinRenderTransparencyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderTransparencyTest CoinRender Coin)
	target_include_directories(CoinRenderTransparencyTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderTransparencyTest COMMAND CoinRenderTransparencyTest)
	set_tests_properties(CoinRenderTransparencyTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
	if(WIN32)
		# Mandatory GL plus the eleven-mode GPU matrix measured 79.6 s on
		# NVIDIA/D3D12; FXC compilation can exceed 90 s with concurrent tests.
		set_tests_properties(CoinRenderTransparencyTest PROPERTIES TIMEOUT 180)
	endif()
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderTransparency_${renderer} COMMAND CoinRenderTransparencyTest)
			set_tests_properties(CoinRenderTransparency_${renderer} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
		endforeach()
	endif()

	add_executable(CoinRenderCompositionTest CoinRenderCompositionTest.cpp)
	set_target_properties(CoinRenderCompositionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderCompositionTest CoinRender Coin)
	target_include_directories(CoinRenderCompositionTest PRIVATE
		${PROJECT_SOURCE_DIR}/testsuite
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderCompositionTest COMMAND CoinRenderCompositionTest)
	set_tests_properties(CoinRenderCompositionTest PROPERTIES TIMEOUT 30)
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderAnnotationTest COMMAND CoinRenderCompositionTest --annotations)
		set_tests_properties(CoinRenderAnnotationTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
	elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer IN ITEMS vulkan opengl)
			add_test(NAME CoinRenderAnnotation_${renderer} COMMAND CoinRenderCompositionTest --annotations)
			set_tests_properties(CoinRenderAnnotation_${renderer} PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
		endforeach()
	endif()

	if(NOT CMAKE_VERSION VERSION_LESS "3.12")
		find_package(Python3 3.7 COMPONENTS Interpreter QUIET)
		if(Python3_Interpreter_FOUND)
			add_test(NAME CoinRenderNodeInventoryDiscoveryTest
				COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/testsuite/coinrender/test_node_inventory.py)
			set_tests_properties(CoinRenderNodeInventoryDiscoveryTest PROPERTIES TIMEOUT 30)
		endif()
	endif()

	add_executable(CoinRenderNodeInventoryTest CoinRenderNodeInventoryTest.cpp)
	set_target_properties(CoinRenderNodeInventoryTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderNodeInventoryTest CoinRender Coin)
	target_include_directories(CoinRenderNodeInventoryTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderNodeInventoryTest COMMAND CoinRenderNodeInventoryTest)
	set_tests_properties(CoinRenderNodeInventoryTest PROPERTIES TIMEOUT 30)
	add_executable(CoinRenderScreenContentTest CoinRenderScreenContentTest.cpp)
	set_target_properties(CoinRenderScreenContentTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderScreenContentTest CoinRender Coin)
	target_include_directories(CoinRenderScreenContentTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderScreenContentCaptureTest COMMAND CoinRenderScreenContentTest --capture)
	set_tests_properties(CoinRenderScreenContentCaptureTest PROPERTIES TIMEOUT 60)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			foreach(renderer ${_coin_bgfx_test_renderers})
				add_test(NAME CoinRenderScreenContent_${renderer} COMMAND CoinRenderScreenContentTest --gpu)
				set_tests_properties(CoinRenderScreenContent_${renderer} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
			endforeach()
		elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
			add_test(NAME CoinRenderScreenContentGpuTest COMMAND CoinRenderScreenContentTest --gpu)
			set_tests_properties(CoinRenderScreenContentGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
	endif()
	add_executable(CoinRenderFragmentPolicyTest CoinRenderFragmentPolicyTest.cpp)
	set_target_properties(CoinRenderFragmentPolicyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderFragmentPolicyTest CoinRender Coin)
	target_include_directories(CoinRenderFragmentPolicyTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderFragmentPolicyCaptureTest COMMAND CoinRenderFragmentPolicyTest --capture)
	set_tests_properties(CoinRenderFragmentPolicyCaptureTest PROPERTIES TIMEOUT 60)
	add_test(NAME CoinRenderDepthDisabledCaptureTest COMMAND CoinRenderFragmentPolicyTest --capture --depth-only)
	set_tests_properties(CoinRenderDepthDisabledCaptureTest PROPERTIES TIMEOUT 60)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			foreach(renderer ${_coin_bgfx_test_renderers})
				add_test(NAME CoinRenderFragmentPolicy_${renderer} COMMAND CoinRenderFragmentPolicyTest --gpu)
				set_tests_properties(CoinRenderFragmentPolicy_${renderer} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
				add_test(NAME CoinRenderDepthDisabled_${renderer} COMMAND CoinRenderFragmentPolicyTest --gpu --depth-only)
				set_tests_properties(CoinRenderDepthDisabled_${renderer} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
			endforeach()
		elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
			add_test(NAME CoinRenderFragmentPolicyGpuTest COMMAND CoinRenderFragmentPolicyTest --gpu)
			set_tests_properties(CoinRenderFragmentPolicyGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			add_test(NAME CoinRenderDepthDisabledGpuTest COMMAND CoinRenderFragmentPolicyTest --gpu --depth-only)
			set_tests_properties(CoinRenderDepthDisabledGpuTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
	endif()
	add_executable(CoinRenderProjectiveUvTest CoinRenderProjectiveUvTest.cpp)
	set_target_properties(CoinRenderProjectiveUvTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderProjectiveUvTest CoinRender Coin)
	target_include_directories(CoinRenderProjectiveUvTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderProjectiveUvCaptureTest COMMAND CoinRenderProjectiveUvTest --capture)
	set_tests_properties(CoinRenderProjectiveUvCaptureTest PROPERTIES TIMEOUT 60)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		target_link_libraries(CoinRenderProjectiveUvTest ${COIN_TARGET_LINK_LIBRARIES_GL})
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			foreach(renderer ${_coin_bgfx_test_renderers})
				add_test(NAME CoinRenderProjectiveUv_${renderer} COMMAND CoinRenderProjectiveUvTest --gpu)
				set_tests_properties(CoinRenderProjectiveUv_${renderer} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
			endforeach()
		elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
			add_test(NAME CoinRenderProjectiveUvGpuTest COMMAND CoinRenderProjectiveUvTest --gpu)
			set_tests_properties(CoinRenderProjectiveUvGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
	endif()
	add_executable(CoinRenderBoundingBoxTest CoinRenderBoundingBoxTest.cpp)
	set_target_properties(CoinRenderBoundingBoxTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderBoundingBoxTest CoinRender Coin)
	target_include_directories(CoinRenderBoundingBoxTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderBoundingBoxCaptureTest COMMAND CoinRenderBoundingBoxTest --capture)
	set_tests_properties(CoinRenderBoundingBoxCaptureTest PROPERTIES TIMEOUT 60)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		target_link_libraries(CoinRenderBoundingBoxTest ${COIN_TARGET_LINK_LIBRARIES_GL})
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			foreach(renderer ${_coin_bgfx_test_renderers})
				add_test(NAME CoinRenderBoundingBox_${renderer} COMMAND CoinRenderBoundingBoxTest --gpu)
				set_tests_properties(CoinRenderBoundingBox_${renderer} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
			endforeach()
		elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
			add_test(NAME CoinRenderBoundingBoxGpuTest COMMAND CoinRenderBoundingBoxTest --gpu)
			set_tests_properties(CoinRenderBoundingBoxGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
	endif()
	add_executable(CoinRenderMarkerSetTest CoinRenderMarkerSetTest.cpp)
	set_target_properties(CoinRenderMarkerSetTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderMarkerSetTest CoinRender Coin)
	target_include_directories(CoinRenderMarkerSetTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderMarkerSetCaptureTest COMMAND CoinRenderMarkerSetTest --capture)
	set_tests_properties(CoinRenderMarkerSetCaptureTest PROPERTIES TIMEOUT 60)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			foreach(renderer ${_coin_bgfx_test_renderers})
				add_test(NAME CoinRenderMarkerSet_${renderer} COMMAND CoinRenderMarkerSetTest --gpu)
				set_tests_properties(CoinRenderMarkerSet_${renderer} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
			endforeach()
		elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
			add_test(NAME CoinRenderMarkerSetGpuTest COMMAND CoinRenderMarkerSetTest --gpu)
			set_tests_properties(CoinRenderMarkerSetGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
	endif()
	add_executable(CoinRenderShadowEightMapTest CoinRenderShadowEightMapTest.cpp)
	set_target_properties(CoinRenderShadowEightMapTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderShadowEightMapTest CoinRender Coin)
	target_include_directories(CoinRenderShadowEightMapTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderShadowEightMapTest COMMAND CoinRenderShadowEightMapTest)
	set_tests_properties(CoinRenderShadowEightMapTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		add_executable(CoinRenderShadowReferenceTest CoinRenderShadowReferenceTest.cpp)
		set_target_properties(CoinRenderShadowReferenceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderShadowReferenceTest CoinRender Coin)
		if(TARGET OpenGL::GL)
			target_link_libraries(CoinRenderShadowReferenceTest OpenGL::GL)
		else()
			target_link_libraries(CoinRenderShadowReferenceTest ${OPENGL_gl_LIBRARY})
		endif()
		target_include_directories(CoinRenderShadowReferenceTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderShadowReferenceTest COMMAND CoinRenderShadowReferenceTest)
		set_tests_properties(CoinRenderShadowReferenceTest PROPERTIES TIMEOUT 60)
		if(WIN32)
			# Native D3D12/FXC compiles the many fixture pipelines in each process.
			set_tests_properties(CoinRenderShadowReferenceTest PROPERTIES TIMEOUT 600)
		endif()
		add_test(NAME CoinRenderShadowWiringTest COMMAND CoinRenderShadowReferenceTest --wiring)
		add_test(NAME CoinRenderPreciseQuadMeshShadowTest COMMAND CoinRenderShadowReferenceTest --precise-quadmesh-shadow)
		set_tests_properties(CoinRenderPreciseQuadMeshShadowTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		add_test(NAME CoinRenderShadowViewportTest COMMAND CoinRenderShadowReferenceTest --viewport)
		set_tests_properties(CoinRenderShadowViewportTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			add_test(NAME CoinRenderShadowViewportOpenGLTest COMMAND CoinRenderShadowReferenceTest --viewport)
			set_tests_properties(CoinRenderShadowViewportOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		endif()
		set_tests_properties(CoinRenderShadowWiringTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
		foreach(_shadow_maps RANGE 5 8)
			add_test(NAME CoinRenderShadowTransparency${_shadow_maps}Test
				COMMAND CoinRenderShadowReferenceTest --transparent-maps ${_shadow_maps})
			set_tests_properties(CoinRenderShadowTransparency${_shadow_maps}Test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
			if(WIN32)
				set_tests_properties(CoinRenderShadowTransparency${_shadow_maps}Test PROPERTIES TIMEOUT 600)
			endif()
		endforeach()
		foreach(_shadow_maps 1 4 5 6 7 8)
			foreach(_alpha_mechanism 0 1 2)
				add_test(NAME CoinRenderShadowAlphaTest${_shadow_maps}_${_alpha_mechanism}Test
					COMMAND CoinRenderShadowReferenceTest --alpha-test ${_shadow_maps} ${_alpha_mechanism})
				set_tests_properties(CoinRenderShadowAlphaTest${_shadow_maps}_${_alpha_mechanism}Test PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
			endforeach()
		endforeach()
		foreach(_shadow_maps 1 4 8)
			add_test(NAME CoinRenderShadowQuality${_shadow_maps}Test
				COMMAND CoinRenderShadowReferenceTest --quality ${_shadow_maps})
			set_tests_properties(CoinRenderShadowQuality${_shadow_maps}Test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
			foreach(_mechanism 1 2)
				add_test(NAME CoinRenderShadowOit${_shadow_maps}_${_mechanism}Test
					COMMAND CoinRenderShadowReferenceTest --shadow-oit ${_shadow_maps} ${_mechanism})
				set_tests_properties(CoinRenderShadowOit${_shadow_maps}_${_mechanism}Test PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
				add_test(NAME CoinRenderShadowLayers${_shadow_maps}_${_mechanism}Test
					COMMAND CoinRenderPeelingTest --shadow ${_shadow_maps} ${_mechanism})
				set_tests_properties(CoinRenderShadowLayers${_shadow_maps}_${_mechanism}Test PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
			endforeach()
		endforeach()
	endif()

	add_executable(CoinRenderMultiTargetTest CoinRenderMultiTargetTest.cpp)
	set_target_properties(CoinRenderMultiTargetTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderMultiTargetTest CoinRender Coin)
	target_include_directories(CoinRenderMultiTargetTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderMultiTargetTest COMMAND CoinRenderMultiTargetTest)
	set_tests_properties(CoinRenderMultiTargetTest PROPERTIES TIMEOUT 120)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderMultiTargetDirectTest COMMAND CoinRenderMultiTargetTest)
		set_tests_properties(CoinRenderMultiTargetDirectTest PROPERTIES TIMEOUT 120 ENVIRONMENT "COIN_RENDER_RTT_GPU_DIRECT=1")
	endif()
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		add_test(NAME CoinRenderMultiTargetStagedOpenGLTest COMMAND CoinRenderMultiTargetTest)
		set_tests_properties(CoinRenderMultiTargetStagedOpenGLTest PROPERTIES TIMEOUT 120 ENVIRONMENT "COIN_BGFX_RENDERER=opengl;COIN_RENDER_RTT_GPU_DIRECT=0")
		add_test(NAME CoinRenderMultiTargetOpenGLTest COMMAND CoinRenderMultiTargetTest)
		set_tests_properties(CoinRenderMultiTargetOpenGLTest PROPERTIES TIMEOUT 120 ENVIRONMENT "COIN_BGFX_RENDERER=opengl;COIN_RENDER_RTT_GPU_DIRECT=1")
	endif()

	add_executable(CoinRenderPublicationTest CoinRenderPublicationTest.cpp)
	set_target_properties(CoinRenderPublicationTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderPublicationTest CoinRender Coin)
	target_include_directories(CoinRenderPublicationTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderPublicationTest COMMAND CoinRenderPublicationTest)
	set_tests_properties(CoinRenderPublicationTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderRttOwnershipTest CoinRenderRttOwnershipTest.cpp)
	set_target_properties(CoinRenderRttOwnershipTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderRttOwnershipTest CoinRender Coin)
	target_include_directories(CoinRenderRttOwnershipTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderRttOwnershipTest COMMAND CoinRenderRttOwnershipTest)
	set_tests_properties(CoinRenderRttOwnershipTest PROPERTIES TIMEOUT 30)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderRttOwnership_${renderer} COMMAND CoinRenderRttOwnershipTest)
			set_tests_properties(CoinRenderRttOwnership_${renderer} PROPERTIES TIMEOUT 30
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
		endforeach()
	endif()


	add_executable(CoinRenderSceneTextureTest CoinRenderSceneTextureTest.cpp)
	set_target_properties(CoinRenderSceneTextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderSceneTextureTest CoinRender Coin)
	target_include_directories(CoinRenderSceneTextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderSceneTextureTest COMMAND CoinRenderSceneTextureTest)
	set_tests_properties(CoinRenderSceneTextureTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderSceneTextureBudgetTest CoinRenderSceneTextureBudgetTest.cpp)
	set_target_properties(CoinRenderSceneTextureBudgetTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderSceneTextureBudgetTest CoinRender Coin)
	target_include_directories(CoinRenderSceneTextureBudgetTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderSceneTextureBudgetTest COMMAND CoinRenderSceneTextureBudgetTest)
	set_tests_properties(CoinRenderSceneTextureBudgetTest PROPERTIES TIMEOUT 240)

	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			foreach(mode staged direct)
				if(mode STREQUAL "direct")
					set(_rtt_direct 1)
				else()
					set(_rtt_direct 0)
				endif()
				foreach(fixture SceneTexture SceneTextureBudget)
					add_test(NAME CoinRender${fixture}_${renderer}_${mode} COMMAND CoinRender${fixture}Test)
					set_tests_properties(CoinRender${fixture}_${renderer}_${mode} PROPERTIES TIMEOUT 240
						ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto;COIN_RENDER_RTT_GPU_DIRECT=${_rtt_direct}")
				endforeach()
			endforeach()
		endforeach()
	endif()

	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderSceneTextureDirectTest COMMAND CoinRenderSceneTextureTest)
		set_tests_properties(CoinRenderSceneTextureDirectTest PROPERTIES
			TIMEOUT 30 ENVIRONMENT "COIN_RENDER_RTT_GPU_DIRECT=1")
		if(WIN32)
			set_tests_properties(CoinRenderSceneTextureDirectTest PROPERTIES TIMEOUT 180)
		endif()
		add_test(NAME CoinRenderSceneTextureBudgetDirectTest COMMAND CoinRenderSceneTextureBudgetTest)
		set_tests_properties(CoinRenderSceneTextureBudgetDirectTest PROPERTIES
			TIMEOUT 240 ENVIRONMENT "COIN_RENDER_RTT_GPU_DIRECT=1")

		add_executable(CoinWgpuFfiFrameTest
			../coinwgpu/CoinWgpuFfiFrameTest.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp

			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinWgpuFfiFrameTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinWgpuFfiFrameTest CoinRender Coin)
		target_include_directories(CoinWgpuFfiFrameTest PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinWgpuFfiFrameTest COMMAND CoinWgpuFfiFrameTest)
		set_tests_properties(CoinWgpuFfiFrameTest PROPERTIES TIMEOUT 30)

		add_executable(CoinWgpuLargeBindingsTest
			../coinwgpu/CoinWgpuLargeBindingsTest.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinWgpuLargeBindingsTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinWgpuLargeBindingsTest CoinRender Coin)
		target_include_directories(CoinWgpuLargeBindingsTest PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinWgpuLargeBindingsTest COMMAND CoinWgpuLargeBindingsTest)
		set_tests_properties(CoinWgpuLargeBindingsTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

		# Measurement utility only: no timing threshold is stable enough for CTest.
		add_executable(CoinRenderFrameReuseBenchmark
			CoinRenderFrameReuseBenchmark.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp

			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderFrameReuseBenchmark PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderFrameReuseBenchmark CoinRender Coin)
		target_include_directories(CoinRenderFrameReuseBenchmark PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)

		add_executable(CoinRenderPerformanceTest CoinRenderPerformanceTest.cpp)
		set_target_properties(CoinRenderPerformanceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderPerformanceTest CoinRender Coin)
		target_include_directories(CoinRenderPerformanceTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinRenderPerformanceTest COMMAND CoinRenderPerformanceTest)
		set_tests_properties(CoinRenderPerformanceTest PROPERTIES TIMEOUT 30)

		add_executable(CoinRenderAsyncReadbackTest CoinRenderAsyncReadbackTest.cpp)
		set_target_properties(CoinRenderAsyncReadbackTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderAsyncReadbackTest CoinRender Coin)
		target_include_directories(CoinRenderAsyncReadbackTest PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinRenderAsyncReadbackTest COMMAND CoinRenderAsyncReadbackTest)
		set_tests_properties(CoinRenderAsyncReadbackTest PROPERTIES TIMEOUT 30)

		add_executable(CoinWgpuMultiDeviceTest
			../coinwgpu/CoinWgpuMultiDeviceTest.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp

			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinWgpuMultiDeviceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinWgpuMultiDeviceTest CoinRender Coin)
		target_include_directories(CoinWgpuMultiDeviceTest PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinWgpuMultiDeviceTest COMMAND CoinWgpuMultiDeviceTest)
		set_tests_properties(CoinWgpuMultiDeviceTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
		add_test(NAME WgpuMultiDeviceStressTest COMMAND CoinWgpuMultiDeviceTest --stress)
		set_tests_properties(WgpuMultiDeviceStressTest PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)

		add_executable(CoinRenderAsyncActionTest CoinRenderAsyncActionTest.cpp)
		set_target_properties(CoinRenderAsyncActionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderAsyncActionTest CoinRender Coin)
		target_include_directories(CoinRenderAsyncActionTest PRIVATE
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinRenderAsyncActionTest COMMAND CoinRenderAsyncActionTest)
		set_tests_properties(CoinRenderAsyncActionTest PROPERTIES TIMEOUT 30)
		add_test(NAME CoinRenderAsyncActionDirectTest COMMAND CoinRenderAsyncActionTest)
		set_tests_properties(CoinRenderAsyncActionDirectTest PROPERTIES
			TIMEOUT 30 ENVIRONMENT "COIN_RENDER_RTT_GPU_DIRECT=1")
		add_test(NAME CoinRenderAsyncActionDirectStressTest COMMAND CoinRenderAsyncActionTest)
		set_tests_properties(CoinRenderAsyncActionDirectStressTest PROPERTIES
			TIMEOUT 240 ENVIRONMENT "COIN_RENDER_RTT_GPU_DIRECT=1;COIN_WGPU_RTT_STRESS=1")
	endif()

	if(COIN_TEST_RENDER_GL_REFERENCE)
		if(NOT (UNIX AND NOT APPLE AND X11_FOUND))
			message(FATAL_ERROR "COIN_TEST_RENDER_GL_REFERENCE requires X11 on Unix")
		endif()
		find_program(COIN_XVFB_RUN_EXECUTABLE xvfb-run)
		if(NOT COIN_XVFB_RUN_EXECUTABLE)
			message(FATAL_ERROR "COIN_TEST_RENDER_GL_REFERENCE requires xvfb-run")
		endif()
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			foreach(renderer ${_coin_bgfx_test_renderers})
				add_test(NAME CoinBgfxReadbackModes_${renderer}_GlReference
					COMMAND ${CMAKE_COMMAND} -E env COIN_BGFX_RENDERER=${renderer} COIN_BGFX_TRANSPARENCY=auto
					COIN_BGFX_COMPARE_GL=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_GLXGLUE_NO_PBUFFERS=1
					${COIN_XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:CoinBgfxReadbackModesTest>)
				set_tests_properties(CoinBgfxReadbackModes_${renderer}_GlReference PROPERTIES TIMEOUT 90)
			endforeach()
		endif()
		add_test(NAME CoinRenderLightingGlReferenceTest
			COMMAND ${CMAKE_COMMAND} -E env
				LIBGL_ALWAYS_SOFTWARE=1
				COIN_GLX_PIXMAP_DIRECT_RENDERING=1
				COIN_GLXGLUE_NO_PBUFFERS=1
				COIN_WGPU_REQUIRE_GL_REFERENCE=1
				${COIN_XVFB_RUN_EXECUTABLE} -a
				-s "-screen 0 1024x768x24 +extension GLX +render -noreset"
				$<TARGET_FILE:CoinRenderLightingTest>)
		set_tests_properties(CoinRenderLightingGlReferenceTest PROPERTIES TIMEOUT 60)
		add_test(NAME CoinRenderFogGlReferenceTest
			COMMAND ${CMAKE_COMMAND} -E env
				LIBGL_ALWAYS_SOFTWARE=1
				COIN_GLX_PIXMAP_DIRECT_RENDERING=1
				COIN_GLXGLUE_NO_PBUFFERS=1
				COIN_WGPU_REQUIRE_GL_REFERENCE=1
				${COIN_XVFB_RUN_EXECUTABLE} -a
				-s "-screen 0 1024x768x24 +extension GLX +render -noreset"
				$<TARGET_FILE:CoinRenderFogTest>)
		set_tests_properties(CoinRenderFogGlReferenceTest PROPERTIES TIMEOUT 60)
	endif()

	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE" AND (UNIX AND NOT APPLE AND X11_FOUND))
		add_executable(CoinRenderSurfaceTest
			CoinRenderSurfaceTest.cpp
		)
		set_target_properties(CoinRenderSurfaceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H;HAVE_X11=1")
		target_link_libraries(CoinRenderSurfaceTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderSurfaceTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${X11_INCLUDE_DIR}
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinRenderSurfaceTest COMMAND CoinRenderSurfaceTest)
		set_tests_properties(CoinRenderSurfaceTest PROPERTIES TIMEOUT 30)

		if(COIN_TEST_RENDER_STRICT_DISPLAY)
			add_test(NAME CoinRenderSurfaceDisplayStrictTest COMMAND CoinRenderSurfaceTest --require-display)
			set_tests_properties(CoinRenderSurfaceDisplayStrictTest PROPERTIES TIMEOUT 30)
			add_test(NAME CoinRenderSurfaceCameraReuseStrictTest COMMAND CoinRenderSurfaceTest --camera-only --require-display)
			set_tests_properties(CoinRenderSurfaceCameraReuseStrictTest PROPERTIES TIMEOUT 30)
		endif()
	endif()
endif()
