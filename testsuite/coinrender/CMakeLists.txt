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

	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE" OR COIN_RENDER_BACKEND STREQUAL "BGFX")
		add_executable(CoinRenderTechnicalMixedAlphaGlReferenceTest CoinRenderTechnicalMixedAlphaGlReferenceTest.cpp)
		set_target_properties(CoinRenderTechnicalMixedAlphaGlReferenceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderTechnicalMixedAlphaGlReferenceTest CoinRender Coin)
		target_include_directories(CoinRenderTechnicalMixedAlphaGlReferenceTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderTechnicalMixedAlphaGlReferenceTest COMMAND CoinRenderTechnicalMixedAlphaGlReferenceTest)
		set_tests_properties(CoinRenderTechnicalMixedAlphaGlReferenceTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderTechnicalScreenDoorOddViewportGlReferenceTest
			COMMAND CoinRenderTechnicalMixedAlphaGlReferenceTest --odd-subviewport)
		set_tests_properties(CoinRenderTechnicalScreenDoorOddViewportGlReferenceTest PROPERTIES
			TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		if(UNIX AND NOT APPLE AND X11_FOUND)
			set_tests_properties(CoinRenderTechnicalMixedAlphaGlReferenceTest PROPERTIES
				ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
			set_tests_properties(CoinRenderTechnicalScreenDoorOddViewportGlReferenceTest PROPERTIES
				ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		endif()
		add_executable(CoinRenderTechnicalBlendLayersGlReferenceTest CoinRenderTechnicalBlendLayersGlReferenceTest.cpp)
		set_target_properties(CoinRenderTechnicalBlendLayersGlReferenceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderTechnicalBlendLayersGlReferenceTest CoinRender Coin)
		target_include_directories(CoinRenderTechnicalBlendLayersGlReferenceTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderTechnicalBlendLayersGlReferenceTest COMMAND CoinRenderTechnicalBlendLayersGlReferenceTest)
		set_tests_properties(CoinRenderTechnicalBlendLayersGlReferenceTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		if(UNIX AND NOT APPLE AND X11_FOUND)
			set_tests_properties(CoinRenderTechnicalBlendLayersGlReferenceTest PROPERTIES
				ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		endif()
		add_executable(CoinRenderManagerLayerViewportGlReferenceTest CoinRenderManagerLayerViewportGlReferenceTest.cpp)
		set_target_properties(CoinRenderManagerLayerViewportGlReferenceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderManagerLayerViewportGlReferenceTest CoinRender Coin)
		target_include_directories(CoinRenderManagerLayerViewportGlReferenceTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderManagerLayerViewportGlReferenceTest COMMAND CoinRenderManagerLayerViewportGlReferenceTest)
		set_tests_properties(CoinRenderManagerLayerViewportGlReferenceTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		if(UNIX AND NOT APPLE AND X11_FOUND)
			set_tests_properties(CoinRenderManagerLayerViewportGlReferenceTest PROPERTIES
				ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		endif()
		add_executable(CoinRenderDrawModesLayersGlReferenceTest CoinRenderDrawModesLayersGlReferenceTest.cpp)
		set_target_properties(CoinRenderDrawModesLayersGlReferenceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderDrawModesLayersGlReferenceTest CoinRender Coin)
		target_include_directories(CoinRenderDrawModesLayersGlReferenceTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderDrawModesLayersGlReferenceTest COMMAND CoinRenderDrawModesLayersGlReferenceTest)
		set_tests_properties(CoinRenderDrawModesLayersGlReferenceTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		if(UNIX AND NOT APPLE AND X11_FOUND)
			set_tests_properties(CoinRenderDrawModesLayersGlReferenceTest PROPERTIES
				ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		endif()
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
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_executable(CoinRenderDrawModesLayersTest CoinRenderDrawModesLayersTest.cpp)
		set_target_properties(CoinRenderDrawModesLayersTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderDrawModesLayersTest CoinRender Coin)
		target_include_directories(CoinRenderDrawModesLayersTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderDrawModesLayersTest COMMAND CoinRenderDrawModesLayersTest)
		set_tests_properties(CoinRenderDrawModesLayersTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderTechnicalLayersTest CoinRenderTechnicalLayersTest.cpp)
		set_target_properties(CoinRenderTechnicalLayersTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderTechnicalLayersTest CoinRender Coin)
		target_include_directories(CoinRenderTechnicalLayersTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderTechnicalLayersTest COMMAND CoinRenderTechnicalLayersTest)
		set_tests_properties(CoinRenderTechnicalLayersTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endif()
	add_executable(CoinRenderStereoEyeTest CoinRenderStereoEyeTest.cpp)
	set_target_properties(CoinRenderStereoEyeTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderStereoEyeTest CoinRender Coin)
	target_include_directories(CoinRenderStereoEyeTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderStereoEyeTest COMMAND CoinRenderStereoEyeTest)
	set_tests_properties(CoinRenderStereoEyeTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)

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
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_executable(CoinRenderMsaaTest CoinRenderMsaaTest.cpp)
		set_target_properties(CoinRenderMsaaTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaTest COMMAND CoinRenderMsaaTest)
		set_tests_properties(CoinRenderMsaaTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
		if(X11_FOUND)
			add_executable(CoinRenderMsaaBumpWindowTest CoinRenderMsaaBumpWindowTest.cpp)
			set_target_properties(CoinRenderMsaaBumpWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(CoinRenderMsaaBumpWindowTest CoinRender Coin ${X11_LIBRARIES})
			target_include_directories(CoinRenderMsaaBumpWindowTest PRIVATE
				${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
				${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
				${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
			add_test(NAME CoinRenderMsaaBumpWindowTest COMMAND CoinRenderMsaaBumpWindowTest)
			set_tests_properties(CoinRenderMsaaBumpWindowTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
		add_executable(CoinRenderMsaaBumpRttTest CoinRenderMsaaBumpRttTest.cpp)
		set_target_properties(CoinRenderMsaaBumpRttTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaBumpRttTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaBumpRttTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaBumpRttTest COMMAND CoinRenderMsaaBumpRttTest)
		set_tests_properties(CoinRenderMsaaBumpRttTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderMsaaBumpTest CoinRenderMsaaBumpTest.cpp)
		set_target_properties(CoinRenderMsaaBumpTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaBumpTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaBumpTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaBumpTest COMMAND CoinRenderMsaaBumpTest)
		set_tests_properties(CoinRenderMsaaBumpTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
		add_executable(CoinRenderMsaaRttTest CoinRenderMsaaRttTest.cpp)
		set_target_properties(CoinRenderMsaaRttTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaRttTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaRttTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaRttTest COMMAND CoinRenderMsaaRttTest)
		set_tests_properties(CoinRenderMsaaRttTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
	endif()

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

	add_executable(CoinRenderFrameCoreTest CoinRenderFrameCoreTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp)
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

	add_executable(CoinRenderLegacyRgbOracleTest CoinRenderLegacyRgbOracleTest.cpp)
	set_target_properties(CoinRenderLegacyRgbOracleTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderLegacyRgbOracleTest CoinRender Coin)
	target_include_directories(CoinRenderLegacyRgbOracleTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderLegacyRgbOracleTest COMMAND CoinRenderLegacyRgbOracleTest)
	set_tests_properties(CoinRenderLegacyRgbOracleTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
		ENVIRONMENT "COIN_OFFSCREENRENDERER_TILEWIDTH=13;COIN_OFFSCREENRENDERER_TILEHEIGHT=9;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		add_executable(CoinRenderTexgenTransitionTest CoinRenderTexgenTransitionTest.cpp)
		target_link_libraries(CoinRenderTexgenTransitionTest Coin)
		if(TARGET OpenGL::GL)
			target_link_libraries(CoinRenderTexgenTransitionTest OpenGL::GL)
		endif()
		target_include_directories(CoinRenderTexgenTransitionTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include)
		add_test(NAME CoinRenderTexgenTransitionTest COMMAND CoinRenderTexgenTransitionTest)
		set_tests_properties(CoinRenderTexgenTransitionTest PROPERTIES
			TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		add_executable(CoinRenderOffscreenCallbackBalanceTest CoinRenderOffscreenCallbackBalanceTest.cpp)
		target_link_libraries(CoinRenderOffscreenCallbackBalanceTest Coin)
		target_include_directories(CoinRenderOffscreenCallbackBalanceTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include)
		add_test(NAME CoinRenderOffscreenCallbackBalanceTest
			COMMAND CoinRenderOffscreenCallbackBalanceTest)
		set_tests_properties(CoinRenderOffscreenCallbackBalanceTest PROPERTIES
			TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		add_test(NAME CoinRenderOffscreenCallbackBalanceTiledTest
			COMMAND CoinRenderOffscreenCallbackBalanceTest --tiled)
		set_tests_properties(CoinRenderOffscreenCallbackBalanceTiledTest PROPERTIES
			TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "COIN_FORCE_TILED_OFFSCREENRENDERING=1;COIN_OFFSCREENRENDERER_TILEWIDTH=13;COIN_OFFSCREENRENDERER_TILEHEIGHT=9;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	endif()

	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
	if(UNIX AND NOT APPLE AND NOT ANDROID AND X11_FOUND)
		add_executable(CoinRenderBgfxMsaaWindowTest CoinRenderBgfxMsaaWindowTest.cpp)
		set_target_properties(CoinRenderBgfxMsaaWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderBgfxMsaaWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderBgfxMsaaWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderBgfxMsaaWindowTest COMMAND CoinRenderBgfxMsaaWindowTest --window)
		set_tests_properties(CoinRenderBgfxMsaaWindowTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
	endif()
	add_executable(CoinRenderBgfxMsaaTest CoinRenderBgfxMsaaTest.cpp)
	add_executable(CoinRenderBgfxMsaaRttTest CoinRenderBgfxMsaaRttTest.cpp)
	target_compile_definitions(CoinRenderBgfxMsaaRttTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderBgfxMsaaRttTest CoinRender Coin)
	target_include_directories(CoinRenderBgfxMsaaRttTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderBgfxMsaaRttTest COMMAND CoinRenderBgfxMsaaRttTest --rtt)
	set_tests_properties(CoinRenderBgfxMsaaRttTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
	target_compile_definitions(CoinRenderBgfxMsaaTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderBgfxMsaaTest CoinRender Coin)
	target_include_directories(CoinRenderBgfxMsaaTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderBgfxMsaaOffscreenTest COMMAND CoinRenderBgfxMsaaTest --offscreen)
	set_tests_properties(CoinRenderBgfxMsaaOffscreenTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
	add_test(NAME CoinRenderBgfxMsaaLifecycleTest COMMAND CoinRenderBgfxMsaaTest --lifecycle)
	set_tests_properties(CoinRenderBgfxMsaaLifecycleTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
	add_test(NAME CoinRenderBgfxMsaaRejectionsTest COMMAND CoinRenderBgfxMsaaTest --rejections)
	set_tests_properties(CoinRenderBgfxMsaaRejectionsTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
	add_test(NAME CoinRenderBgfxMsaaProfilesTest COMMAND CoinRenderBgfxMsaaTest --profiles)
	set_tests_properties(CoinRenderBgfxMsaaProfilesTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
	add_executable(CoinRenderSolidSelectionTest CoinRenderSolidSelectionTest.cpp)
	add_executable(CoinRenderTexture2dSelectionTest CoinRenderTexture2dSelectionTest.cpp)
	set_target_properties(CoinRenderTexture2dSelectionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderTexture2dSelectionTest CoinRender Coin)
	target_include_directories(CoinRenderTexture2dSelectionTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(renderer ${_coin_bgfx_test_renderers})
		add_test(NAME CoinRenderTexture2dSelection_${renderer}
			COMMAND CoinRenderTexture2dSelectionTest)
		add_test(NAME CoinRenderTexture2dSelectionCanonical_${renderer}
			COMMAND CoinRenderTexture2dSelectionTest --canonical-raster)
		add_test(NAME CoinRenderTexture2dSelectionPortable_${renderer}
			COMMAND CoinRenderTexture2dSelectionTest --portable-sampling)
		add_test(NAME CoinRenderTexture2dSelectionPortableCanonical_${renderer}
			COMMAND CoinRenderTexture2dSelectionTest --portable-sampling --canonical-raster)
		set_tests_properties(CoinRenderTexture2dSelection_${renderer}
			CoinRenderTexture2dSelectionCanonical_${renderer}
			CoinRenderTexture2dSelectionPortable_${renderer}
			CoinRenderTexture2dSelectionPortableCanonical_${renderer}
			PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
	endforeach()
	set_target_properties(CoinRenderSolidSelectionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderSolidSelectionTest CoinRender Coin)
	target_include_directories(CoinRenderSolidSelectionTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(renderer ${_coin_bgfx_test_renderers})
		add_test(NAME CoinRenderSolidSelection_${renderer}
			COMMAND CoinRenderSolidSelectionTest)
		add_test(NAME CoinRenderSolidSelectionCanonical_${renderer}
			COMMAND CoinRenderSolidSelectionTest --canonical-raster)
		set_tests_properties(CoinRenderSolidSelection_${renderer}
			CoinRenderSolidSelectionCanonical_${renderer}
			PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
	endforeach()
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
	add_executable(CoinBgfxInstancedPayloadTest ../coinbgfx/CoinBgfxInstancedPayloadTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxLowering.cpp)
	set_target_properties(CoinBgfxInstancedPayloadTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxInstancedPayloadTest CoinRender Coin)
	target_include_directories(CoinBgfxInstancedPayloadTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinBgfxInstancedPayloadTest COMMAND CoinBgfxInstancedPayloadTest)
	set_tests_properties(CoinBgfxInstancedPayloadTest PROPERTIES TIMEOUT 30)
	add_executable(CoinBgfxInstancedStressTest ../coinbgfx/CoinBgfxInstancedStressTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxLowering.cpp)
	set_target_properties(CoinBgfxInstancedStressTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxInstancedStressTest CoinRender Coin)
	target_include_directories(CoinBgfxInstancedStressTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinBgfxInstancedStressTest COMMAND CoinBgfxInstancedStressTest)
	set_tests_properties(CoinBgfxInstancedStressTest PROPERTIES TIMEOUT 30)
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
	add_executable(CoinBgfxSolidEligibilityTest ../coinbgfx/CoinBgfxSolidEligibilityTest.cpp)
	target_sources(CoinBgfxSolidEligibilityTest PRIVATE
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxProgramSelection.cpp)
	set_target_properties(CoinBgfxSolidEligibilityTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxSolidEligibilityTest CoinRender Coin)
	target_include_directories(CoinBgfxSolidEligibilityTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src)
	add_test(NAME CoinBgfxSolidEligibilityTest COMMAND CoinBgfxSolidEligibilityTest)
	set_tests_properties(CoinBgfxSolidEligibilityTest PROPERTIES TIMEOUT 30)
	add_executable(CoinBgfxTexture2dEligibilityTest ../coinbgfx/CoinBgfxTexture2dEligibilityTest.cpp)
	target_sources(CoinBgfxTexture2dEligibilityTest PRIVATE
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxProgramSelection.cpp)
	set_target_properties(CoinBgfxTexture2dEligibilityTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxTexture2dEligibilityTest CoinRender Coin)
	target_include_directories(CoinBgfxTexture2dEligibilityTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src)
	add_test(NAME CoinBgfxTexture2dEligibilityTest COMMAND CoinBgfxTexture2dEligibilityTest)
	set_tests_properties(CoinBgfxTexture2dEligibilityTest PROPERTIES TIMEOUT 30)
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
		add_test(NAME CoinBgfxCanonicalInstancingVulkanTest COMMAND CoinBgfxInstancingTest)
		add_test(NAME CoinBgfxCanonicalInstancingOpenGLTest COMMAND CoinBgfxInstancingTest)
		set_tests_properties(CoinBgfxCanonicalInstancingVulkanTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_BGFX_RENDERER=vulkan;COIN_RENDER_INSTANCING_CANONICAL_COVERAGE=1")
		set_tests_properties(CoinBgfxCanonicalInstancingOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_BGFX_RENDERER=opengl;COIN_RENDER_INSTANCING_CANONICAL_COVERAGE=1")
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
			foreach(_pixels_renderer vulkan opengl)
				add_test(NAME CoinBgfxMultiWindowNativePixels_${_pixels_renderer}Test COMMAND CoinBgfxMultiWindowTest)
				set_tests_properties(CoinBgfxMultiWindowNativePixels_${_pixels_renderer}Test PROPERTIES
					TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${_pixels_renderer};COIN_BGFX_TEST_REQUIRE_X11_PIXELS=1")
			endforeach()
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
	add_executable(CoinRenderCubeTextureCoreTest CoinRenderCubeTextureCoreTest.cpp)
	set_target_properties(CoinRenderCubeTextureCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderCubeTextureCoreTest CoinRender Coin)
	target_include_directories(CoinRenderCubeTextureCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderCubeTextureCoreTest COMMAND CoinRenderCubeTextureCoreTest)
	set_tests_properties(CoinRenderCubeTextureCoreTest PROPERTIES TIMEOUT 180)
	add_executable(CoinRenderCubeIngressTest CoinRenderCubeIngressTest.cpp)
	target_compile_definitions(CoinRenderCubeIngressTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderCubeIngressTest CoinRender Coin)
	target_include_directories(CoinRenderCubeIngressTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderCubeIngressTest COMMAND CoinRenderCubeIngressTest)
	set_tests_properties(CoinRenderCubeIngressTest PROPERTIES TIMEOUT 90)
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderAdvancedTextureGpuTest COMMAND CoinRenderAdvancedTextureTest --gpu)
	elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderAdvancedTexture_${renderer} COMMAND CoinRenderAdvancedTextureTest --gpu)
			set_tests_properties(CoinRenderAdvancedTexture_${renderer} PROPERTIES ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
		endforeach()
	endif()

	if(COIN_BUILD_LEGACY_GL_RENDERER)
	add_executable(CoinRenderConcaveAlphaTest CoinRenderConcaveAlphaTest.cpp)
	target_compile_definitions(CoinRenderConcaveAlphaTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderConcaveAlphaTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderConcaveAlphaTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderConcaveAlphaTest COMMAND CoinRenderConcaveAlphaTest --gpu)
	set_tests_properties(CoinRenderConcaveAlphaTest PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	add_test(NAME CoinRenderConcaveAlphaCanonicalTest COMMAND CoinRenderConcaveAlphaTest --gpu --canonical-raster)
	set_tests_properties(CoinRenderConcaveAlphaCanonicalTest PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	add_executable(CoinRenderConcaveStrokeTest CoinRenderConcaveStrokeTest.cpp)
	target_compile_definitions(CoinRenderConcaveStrokeTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderConcaveStrokeTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderConcaveStrokeTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderConcaveStrokeTest COMMAND CoinRenderConcaveStrokeTest)
	set_tests_properties(CoinRenderConcaveStrokeTest PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	add_test(NAME CoinRenderConcaveStrokeCanonicalTest COMMAND CoinRenderConcaveStrokeTest --canonical-raster)
	set_tests_properties(CoinRenderConcaveStrokeCanonicalTest PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	add_executable(CoinRenderPreciseQuadMeshMultitextureTest CoinRenderPreciseQuadMeshMultitextureTest.cpp)
	target_compile_definitions(CoinRenderPreciseQuadMeshMultitextureTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderPreciseQuadMeshMultitextureTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderPreciseQuadMeshMultitextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderPreciseQuadMeshMultitextureTest COMMAND CoinRenderPreciseQuadMeshMultitextureTest)
	add_test(NAME CoinRenderPreciseQuadMeshEightUnitExecutorTest
		COMMAND CoinRenderPreciseQuadMeshMultitextureTest --eight-unit-executor-reference)
	set_tests_properties(CoinRenderPreciseQuadMeshMultitextureTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	set_tests_properties(CoinRenderPreciseQuadMeshEightUnitExecutorTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	add_executable(CoinRenderVolumeNpotCoreTest CoinRenderVolumeNpotCoreTest.cpp)
	target_compile_definitions(CoinRenderVolumeNpotCoreTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderVolumeNpotCoreTest CoinRender Coin)
	target_include_directories(CoinRenderVolumeNpotCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderVolumeNpotCoreTest COMMAND CoinRenderVolumeNpotCoreTest)
	add_test(NAME CoinRenderVolumeNpotLimitsTest COMMAND CoinRenderVolumeNpotCoreTest --limits)
	set_tests_properties(CoinRenderVolumeNpotLimitsTest PROPERTIES TIMEOUT 300)
	add_executable(CoinRenderVolumeNpotTest CoinRenderVolumeNpotTest.cpp)
	target_compile_definitions(CoinRenderVolumeNpotTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderVolumeNpotTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderVolumeNpotTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderVolumeNpotCpuTest COMMAND CoinRenderVolumeNpotTest --cpu)
	add_test(NAME CoinRenderVolumeNpotTest COMMAND CoinRenderVolumeNpotTest)
	add_test(NAME CoinRenderVolumeNpotPortableTest COMMAND CoinRenderVolumeNpotTest --portable-sampling)
	set_tests_properties(CoinRenderVolumeNpotTest CoinRenderVolumeNpotPortableTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_RENDER_REQUIRE_TEXTURE3_GPU=1")
	add_executable(CoinRenderTexture3ReferenceTest CoinRenderTexture3ReferenceTest.cpp)
	add_executable(CoinRenderTextureQualityTransitionTest CoinRenderTextureQualityTransitionTest.cpp)
	add_executable(CoinRenderTextureQualityStateTest CoinRenderTextureQualityStateTest.cpp)
	target_compile_definitions(CoinRenderTextureQualityStateTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderTextureQualityStateTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderTextureQualityStateTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(_quality_case contexts separator flags upgrade)
		add_test(NAME CoinRenderTextureQuality${_quality_case}Test
			COMMAND CoinRenderTextureQualityStateTest --${_quality_case})
		set_tests_properties(CoinRenderTextureQuality${_quality_case}Test PROPERTIES TIMEOUT 120
			RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	endforeach()
	target_compile_definitions(CoinRenderTextureQualityTransitionTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderTextureQualityTransitionTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderTextureQualityTransitionTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderTextureQualityTransitionUnit0Test COMMAND CoinRenderTextureQualityTransitionTest --unit0)
	add_test(NAME CoinRenderTextureQualityTransitionUnit3Test COMMAND CoinRenderTextureQualityTransitionTest --unit3)
	set_tests_properties(CoinRenderTextureQualityTransitionUnit0Test CoinRenderTextureQualityTransitionUnit3Test
		PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	add_executable(CoinRenderCubeTextureReferenceTest CoinRenderCubeTextureReferenceTest.cpp)
	target_compile_definitions(CoinRenderCubeTextureReferenceTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderCubeTextureReferenceTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderCubeTextureReferenceTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderCubeTextureCaptureTest COMMAND CoinRenderCubeTextureReferenceTest --capture)
	add_test(NAME CoinRenderCubeTextureGpuTest COMMAND CoinRenderCubeTextureReferenceTest --gpu)
	add_test(NAME CoinRenderCubeTextureGlReferenceTest COMMAND CoinRenderCubeTextureReferenceTest --gpu --gl-reference)
	add_test(NAME CoinRenderCubeTextureUnit7Test COMMAND CoinRenderCubeTextureReferenceTest --gpu --unit7)
	add_test(NAME CoinRenderCubeTextureMixedTest COMMAND CoinRenderCubeTextureReferenceTest --gpu --mixed)
	add_test(NAME CoinRenderCubeTextureFilterTest COMMAND CoinRenderCubeTextureReferenceTest --gpu --filters)
	add_test(NAME CoinRenderCubeTextureMinificationTest COMMAND CoinRenderCubeTextureReferenceTest --gpu --minification)
	add_test(NAME CoinRenderCubeTextureMaximumTest COMMAND CoinRenderCubeTextureReferenceTest --gpu --maximum)
	set_tests_properties(CoinRenderCubeTextureCaptureTest CoinRenderCubeTextureGpuTest
		CoinRenderCubeTextureGlReferenceTest CoinRenderCubeTextureUnit7Test
		CoinRenderCubeTextureMixedTest CoinRenderCubeTextureFilterTest
		CoinRenderCubeTextureMinificationTest PROPERTIES TIMEOUT 180 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_RENDER_REQUIRE_CUBE_GPU=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	set_tests_properties(CoinRenderCubeTextureMaximumTest PROPERTIES TIMEOUT 600 RESOURCE_LOCK coinrender_gpu
		ENVIRONMENT "COIN_RENDER_REQUIRE_CUBE_GPU=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	target_compile_definitions(CoinRenderTexture3ReferenceTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderTexture3ReferenceTest CoinRender Coin OpenGL::GL)
	target_include_directories(CoinRenderTexture3ReferenceTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderTexture3ReferenceTest COMMAND CoinRenderTexture3ReferenceTest)
	set_tests_properties(CoinRenderTexture3ReferenceTest PROPERTIES TIMEOUT 600 RESOURCE_LOCK coinrender_gpu)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		set_tests_properties(CoinRenderTexture3ReferenceTest PROPERTIES
			ENVIRONMENT "COIN_RENDER_REQUIRE_TEXTURE3_GPU=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
	endif()
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
	add_executable(CoinRenderPeelingDepthStateTest CoinRenderPeelingDepthStateTest.cpp)
	set_target_properties(CoinRenderPeelingDepthStateTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderPeelingDepthStateTest CoinRender Coin)
	target_include_directories(CoinRenderPeelingDepthStateTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderPeelingDepthStateTest COMMAND CoinRenderPeelingDepthStateTest)
	set_tests_properties(CoinRenderPeelingDepthStateTest PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
	add_test(NAME CoinRenderPeelingDepthStateCanonicalTest COMMAND CoinRenderPeelingDepthStateTest --canonical-raster)
	set_tests_properties(CoinRenderPeelingDepthStateCanonicalTest PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
	foreach(mode staged direct)
		add_test(NAME CoinRenderPeelingDepthRtt_${mode} COMMAND CoinRenderPeelingDepthStateTest --rtt-${mode})
		set_tests_properties(CoinRenderPeelingDepthRtt_${mode} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
		add_test(NAME CoinRenderPeelingDepthRttCanonical_${mode}
			COMMAND CoinRenderPeelingDepthStateTest --rtt-${mode} --canonical-raster)
		set_tests_properties(CoinRenderPeelingDepthRttCanonical_${mode} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
	endforeach()
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer ${_coin_bgfx_test_renderers})
			add_test(NAME CoinRenderPeelingDepthState_${renderer} COMMAND CoinRenderPeelingDepthStateTest)
			set_tests_properties(CoinRenderPeelingDepthState_${renderer} PROPERTIES
				TIMEOUT 600 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
			add_test(NAME CoinRenderPeelingDepthStateCanonical_${renderer}
				COMMAND CoinRenderPeelingDepthStateTest --canonical-raster)
			set_tests_properties(CoinRenderPeelingDepthStateCanonical_${renderer} PROPERTIES
				TIMEOUT 600 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
			foreach(mode staged direct)
				add_test(NAME CoinRenderPeelingDepthRtt_${renderer}_${mode}
					COMMAND CoinRenderPeelingDepthStateTest --rtt-${mode})
				set_tests_properties(CoinRenderPeelingDepthRtt_${renderer}_${mode} PROPERTIES
					TIMEOUT 180 SKIP_RETURN_CODE 77
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
				add_test(NAME CoinRenderPeelingDepthRttCanonical_${renderer}_${mode}
					COMMAND CoinRenderPeelingDepthStateTest --rtt-${mode} --canonical-raster)
				set_tests_properties(CoinRenderPeelingDepthRttCanonical_${renderer}_${mode} PROPERTIES
					TIMEOUT 180 SKIP_RETURN_CODE 77
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
			endforeach()
		endforeach()
	endif()
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

	# Manual P20 oracle capture; accepts the checksummed historical fixture directory.
	add_executable(CoinRenderP20ReferenceCapture CoinRenderP20ReferenceCapture.cpp)
	set_target_properties(CoinRenderP20ReferenceCapture PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderP20ReferenceCapture CoinRender Coin)
	target_include_directories(CoinRenderP20ReferenceCapture PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})

	if(UNIX AND NOT APPLE AND NOT ANDROID AND X11_FOUND AND
	   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
		add_executable(CoinRenderImageInheritedWindowTest CoinRenderImageInheritedWindowTest.cpp)
		add_executable(CoinRenderVolumeNpotWindowTest CoinRenderVolumeNpotWindowTest.cpp)
		target_compile_definitions(CoinRenderVolumeNpotWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderVolumeNpotWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderVolumeNpotWindowTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderVolumeNpotWindowTest COMMAND CoinRenderVolumeNpotWindowTest --readback)
		add_test(NAME CoinRenderVolumeNpotWindowPortableTest COMMAND CoinRenderVolumeNpotWindowTest --readback --portable-sampling)
		add_test(NAME CoinRenderVolumeNpotWindowMsaa2Test COMMAND CoinRenderVolumeNpotWindowTest --readback --msaa2)
		add_test(NAME CoinRenderVolumeNpotWindowMsaa4Test COMMAND CoinRenderVolumeNpotWindowTest --readback --msaa4 --portable-sampling)
		set_tests_properties(CoinRenderVolumeNpotWindowTest CoinRenderVolumeNpotWindowPortableTest CoinRenderVolumeNpotWindowMsaa2Test CoinRenderVolumeNpotWindowMsaa4Test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			add_executable(CoinRenderTexture2dWindowTest CoinRenderTexture2dWindowTest.cpp)
			set_target_properties(CoinRenderTexture2dWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(CoinRenderTexture2dWindowTest CoinRender Coin ${X11_LIBRARIES})
			target_include_directories(CoinRenderTexture2dWindowTest PRIVATE
				${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
				${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
				${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
			foreach(renderer ${_coin_bgfx_test_renderers})
				add_test(NAME CoinRenderTexture2dWindow_${renderer} COMMAND CoinRenderTexture2dWindowTest --readback)
				add_test(NAME CoinRenderTexture2dWindowCanonical_${renderer}
					COMMAND CoinRenderTexture2dWindowTest --canonical-raster --readback)
				add_test(NAME CoinRenderTexture2dWindowPortable_${renderer}
					COMMAND CoinRenderTexture2dWindowTest --portable-sampling --readback)
				add_test(NAME CoinRenderTexture2dWindowPortableCanonical_${renderer}
					COMMAND CoinRenderTexture2dWindowTest --portable-sampling --canonical-raster --readback)
				set_tests_properties(CoinRenderTexture2dWindow_${renderer}
					CoinRenderTexture2dWindowCanonical_${renderer}
					CoinRenderTexture2dWindowPortable_${renderer}
					CoinRenderTexture2dWindowPortableCanonical_${renderer}
					PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
			endforeach()
		endif()
		set_target_properties(CoinRenderImageInheritedWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderImageInheritedWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderImageInheritedWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderImageInheritedWindowTest COMMAND CoinRenderImageInheritedWindowTest)
		add_test(NAME CoinRenderImageInheritedPlainWindowTest COMMAND CoinRenderImageInheritedWindowTest --plain)
		add_test(NAME CoinRenderImageInheritedClearWindowTest COMMAND CoinRenderImageInheritedWindowTest --clear)
		add_test(NAME CoinRenderImageInheritedGeometryWindowTest COMMAND CoinRenderImageInheritedWindowTest --geometry)
		add_test(NAME CoinRenderImageInheritedTexturedGeometryWindowTest COMMAND CoinRenderImageInheritedWindowTest --textured-geometry)
		add_test(NAME CoinRenderImageInheritedReadbackWindowTest COMMAND CoinRenderImageInheritedWindowTest --window-readback)
		add_test(NAME CoinRenderImageInheritedPlainReadbackWindowTest COMMAND CoinRenderImageInheritedWindowTest --plain --window-readback)
		add_test(NAME CoinRenderImageInheritedClearReadbackWindowTest COMMAND CoinRenderImageInheritedWindowTest --clear --window-readback)
		add_test(NAME CoinRenderImageInheritedGeometryReadbackWindowTest COMMAND CoinRenderImageInheritedWindowTest --geometry --window-readback)
		add_test(NAME CoinRenderImageInheritedTexturedGeometryReadbackWindowTest COMMAND CoinRenderImageInheritedWindowTest --textured-geometry --window-readback)
		set_tests_properties(CoinRenderImageInheritedWindowTest CoinRenderImageInheritedPlainWindowTest
			CoinRenderImageInheritedClearWindowTest CoinRenderImageInheritedGeometryWindowTest
			CoinRenderImageInheritedTexturedGeometryWindowTest CoinRenderImageInheritedReadbackWindowTest
			CoinRenderImageInheritedPlainReadbackWindowTest CoinRenderImageInheritedClearReadbackWindowTest
			CoinRenderImageInheritedGeometryReadbackWindowTest CoinRenderImageInheritedTexturedGeometryReadbackWindowTest
			PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderPeelingDepthWindowTest CoinRenderPeelingDepthWindowTest.cpp)
		set_target_properties(CoinRenderPeelingDepthWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderPeelingDepthWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderPeelingDepthWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderPeelingDepthWindowTest COMMAND CoinRenderPeelingDepthWindowTest)
		set_tests_properties(CoinRenderPeelingDepthWindowTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderPeelingDepthWindowCanonicalTest COMMAND CoinRenderPeelingDepthWindowTest --canonical-raster)
		set_tests_properties(CoinRenderPeelingDepthWindowCanonicalTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderTechnicalAlphaLayersWindowTest CoinRenderTechnicalAlphaLayersWindowTest.cpp)
		set_target_properties(CoinRenderTechnicalAlphaLayersWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderTechnicalAlphaLayersWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderTechnicalAlphaLayersWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
			${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderTechnicalAlphaLayersWindowTest COMMAND CoinRenderTechnicalAlphaLayersWindowTest)
		set_tests_properties(CoinRenderTechnicalAlphaLayersWindowTest PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
			add_executable(CoinRenderDrawModesLayersWindowTest CoinRenderDrawModesLayersWindowTest.cpp)
			set_target_properties(CoinRenderDrawModesLayersWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(CoinRenderDrawModesLayersWindowTest CoinRender Coin ${X11_LIBRARIES})
			target_include_directories(CoinRenderDrawModesLayersWindowTest PRIVATE
				${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
				${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
				${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
			add_test(NAME CoinRenderDrawModesLayersWindowTest COMMAND CoinRenderDrawModesLayersWindowTest)
			set_tests_properties(CoinRenderDrawModesLayersWindowTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			add_executable(CoinRenderTechnicalLayersWindowTest CoinRenderTechnicalLayersWindowTest.cpp)
			set_target_properties(CoinRenderTechnicalLayersWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(CoinRenderTechnicalLayersWindowTest CoinRender Coin ${X11_LIBRARIES})
			target_include_directories(CoinRenderTechnicalLayersWindowTest PRIVATE
				${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
				${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
				${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
			add_test(NAME CoinRenderTechnicalLayersWindowTest COMMAND CoinRenderTechnicalLayersWindowTest)
			set_tests_properties(CoinRenderTechnicalLayersWindowTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
		add_executable(CoinRenderManagerWindowLayersTest CoinRenderManagerWindowLayersTest.cpp)
		add_executable(CoinRenderMsaaLayersTest CoinRenderMsaaLayersTest.cpp)
		add_executable(CoinRenderMsaaTransparencyTest CoinRenderMsaaTransparencyTest.cpp)
		add_executable(CoinRenderMsaaWeightedTest CoinRenderMsaaWeightedTest.cpp)
		add_executable(CoinRenderInheritedSurfaceSamplingTest CoinRenderInheritedSurfaceSamplingTest.cpp)
		target_sources(CoinRenderInheritedSurfaceSamplingTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		target_compile_definitions(CoinRenderInheritedSurfaceSamplingTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderInheritedSurfaceSamplingTest CoinRender Coin)
		target_include_directories(CoinRenderInheritedSurfaceSamplingTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(inherited_samples 2 4)
			add_test(NAME CoinRenderInheritedSurfaceCpu${inherited_samples} COMMAND CoinRenderInheritedSurfaceSamplingTest --cpu --samples ${inherited_samples})
			set_tests_properties(CoinRenderInheritedSurfaceCpu${inherited_samples} PROPERTIES TIMEOUT 120)
			foreach(inherited_configuration RANGE 0 5)
				add_test(NAME CoinRenderInheritedSurfaceMsaa${inherited_samples}_${inherited_configuration}
					COMMAND CoinRenderInheritedSurfaceSamplingTest --gpu --samples ${inherited_samples} --configuration ${inherited_configuration})
				set_tests_properties(CoinRenderInheritedSurfaceMsaa${inherited_samples}_${inherited_configuration}
					PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			endforeach()
		endforeach()
		foreach(inherited_route staged direct)
			foreach(inherited_configuration RANGE 0 5)
				add_test(NAME CoinRenderInheritedSurfaceRtt_${inherited_route}_${inherited_configuration}
					COMMAND CoinRenderInheritedSurfaceSamplingTest --gpu --domain ${inherited_route} --configuration ${inherited_configuration})
				set_tests_properties(CoinRenderInheritedSurfaceRtt_${inherited_route}_${inherited_configuration}
					PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			endforeach()
		endforeach()
		add_executable(CoinRenderInheritedShadowSamplingTest CoinRenderInheritedShadowSamplingTest.cpp)
		target_sources(CoinRenderInheritedShadowSamplingTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		target_compile_definitions(CoinRenderInheritedShadowSamplingTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderInheritedShadowSamplingTest CoinRender Coin)
		target_include_directories(CoinRenderInheritedShadowSamplingTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderInheritedShadowSamplingCore COMMAND CoinRenderInheritedShadowSamplingTest --core)
		set_tests_properties(CoinRenderInheritedShadowSamplingCore PROPERTIES TIMEOUT 120)
		add_test(NAME CoinRenderInheritedShadowSamplingUnit0 COMMAND CoinRenderInheritedShadowSamplingTest --gpu --maps 1 --light spot --unit 0 --quality 0 --wrap repeat)
		add_test(NAME CoinRenderInheritedShadowSamplingUnit3 COMMAND CoinRenderInheritedShadowSamplingTest --gpu --maps 4 --light directional --unit 3 --quality 1 --wrap clamp)
		add_test(NAME CoinRenderInheritedShadowSamplingUnit7Mips COMMAND CoinRenderInheritedShadowSamplingTest --gpu --maps 8 --light spot --unit 7 --quality 2 --wrap repeat)
		add_test(NAME CoinRenderInheritedShadowSamplingAllMips COMMAND CoinRenderInheritedShadowSamplingTest --gpu --maps 8 --light directional --unit all --quality 3 --wrap clamp)
		add_test(NAME CoinRenderInheritedShadowSamplingAllHolesPortable COMMAND CoinRenderInheritedShadowSamplingTest --gpu --maps 4 --light spot --unit all --quality 1 --wrap repeat --holes --portable-sampling)
		add_test(NAME CoinRenderInheritedShadowSamplingUnit7HolesPortable COMMAND CoinRenderInheritedShadowSamplingTest --gpu --maps 1 --light directional --unit 7 --quality 3 --wrap clamp --holes --portable-sampling)
		set_tests_properties(CoinRenderInheritedShadowSamplingUnit0 CoinRenderInheritedShadowSamplingUnit3
			CoinRenderInheritedShadowSamplingUnit7Mips CoinRenderInheritedShadowSamplingAllMips
			CoinRenderInheritedShadowSamplingAllHolesPortable CoinRenderInheritedShadowSamplingUnit7HolesPortable
			PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderShadowSpatialTest CoinRenderShadowSpatialTest.cpp)
		target_sources(CoinRenderShadowSpatialTest PRIVATE ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		target_compile_definitions(CoinRenderShadowSpatialTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderShadowSpatialTest CoinRender Coin)
		target_include_directories(CoinRenderShadowSpatialTest PRIVATE ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderShadowSpatialTransportTest COMMAND CoinRenderShadowSpatialTest --transport)
		add_test(NAME CoinRenderShadowSpatialSmokeTest COMMAND CoinRenderShadowSpatialTest --smoke)
		add_test(NAME CoinRenderShadowSpatialSmokePortableTest COMMAND CoinRenderShadowSpatialTest --smoke --portable-sampling)
		add_test(NAME CoinRenderShadowSpatialAlphaObjectTest COMMAND CoinRenderShadowSpatialTest --alpha --method object --samples 1)
		add_test(NAME CoinRenderShadowSpatialAlphaWeightedTest COMMAND CoinRenderShadowSpatialTest --alpha --method weighted --samples 4)
		add_test(NAME CoinRenderShadowSpatialAlphaPeelingTest COMMAND CoinRenderShadowSpatialTest --alpha --method peeling --samples 2)
		add_test(NAME CoinRenderShadowSpatialRttStagedTest COMMAND CoinRenderShadowSpatialTest --rtt --staged)
		add_test(NAME CoinRenderShadowSpatialRttDirectTest COMMAND CoinRenderShadowSpatialTest --rtt --direct)
		foreach(spatial_alpha_route direct staged)
			foreach(spatial_alpha_method object weighted peeling)
				add_test(NAME CoinRenderShadowSpatialRttAlpha_${spatial_alpha_route}_${spatial_alpha_method} COMMAND CoinRenderShadowSpatialTest --rtt-alpha --${spatial_alpha_route} --method ${spatial_alpha_method} --samples 2)
				set_tests_properties(CoinRenderShadowSpatialRttAlpha_${spatial_alpha_route}_${spatial_alpha_method} PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
			endforeach()
		endforeach()

		set_tests_properties(CoinRenderShadowSpatialSmokePortableTest CoinRenderShadowSpatialAlphaObjectTest CoinRenderShadowSpatialAlphaWeightedTest CoinRenderShadowSpatialAlphaPeelingTest CoinRenderShadowSpatialRttStagedTest CoinRenderShadowSpatialRttDirectTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")

		foreach(spatial_mode projection filters cube-mips wrap models replace-alpha combine invalid maps depth scope)
			add_test(NAME CoinRenderShadowSpatial_${spatial_mode} COMMAND CoinRenderShadowSpatialTest --${spatial_mode})
			set_tests_properties(CoinRenderShadowSpatial_${spatial_mode} PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		endforeach()

		set_tests_properties(CoinRenderShadowSpatialSmokeTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		add_executable(CoinRenderShadowMultitextureTest CoinRenderShadowMultitextureTest.cpp)
		target_sources(CoinRenderShadowMultitextureTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderShadowMultitextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderShadowMultitextureTest CoinRender Coin)
		target_include_directories(CoinRenderShadowMultitextureTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(mode composition bindings slots transport rollback smoke)
			add_test(NAME CoinRenderShadowMultitexture_${mode} COMMAND CoinRenderShadowMultitextureTest --${mode})
			set_tests_properties(CoinRenderShadowMultitexture_${mode} PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
			add_test(NAME CoinRenderShadowMultitexture_${mode}_Portable
				COMMAND CoinRenderShadowMultitextureTest --${mode} --portable-sampling)
			set_tests_properties(CoinRenderShadowMultitexture_${mode}_Portable PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
			add_test(NAME CoinRenderShadowMultitexture_${mode}_Strip COMMAND CoinRenderShadowMultitextureTest --${mode})
			set_tests_properties(CoinRenderShadowMultitexture_${mode}_Strip PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=0")
			add_test(NAME CoinRenderShadowMultitexture_${mode}_Portable_Strip
				COMMAND CoinRenderShadowMultitextureTest --${mode} --portable-sampling)
			set_tests_properties(CoinRenderShadowMultitexture_${mode}_Portable_Strip PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=0")
		endforeach()
		add_executable(CoinRenderShadowAdditiveCompositionTest CoinRenderShadowAdditiveCompositionTest.cpp)
		target_sources(CoinRenderShadowAdditiveCompositionTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderShadowAdditiveCompositionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderShadowAdditiveCompositionTest CoinRender Coin)
		target_include_directories(CoinRenderShadowAdditiveCompositionTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderShadowAdditiveCompositionTest COMMAND CoinRenderShadowAdditiveCompositionTest --campaign)
		set_tests_properties(CoinRenderShadowAdditiveCompositionTest PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		foreach(samples 2 4)
			add_test(NAME CoinRenderShadowAdditiveComposition_MSAA${samples}
				COMMAND CoinRenderShadowAdditiveCompositionTest --campaign --samples ${samples})
			set_tests_properties(CoinRenderShadowAdditiveComposition_MSAA${samples} PROPERTIES
				TIMEOUT 900 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endforeach()
		foreach(route staged direct)
			foreach(samples 1 2 4)
				add_test(NAME CoinRenderShadowAdditiveComposition_Rtt_${route}_MSAA${samples}
					COMMAND CoinRenderShadowAdditiveCompositionTest --campaign --rtt-${route} --samples ${samples})
				set_tests_properties(CoinRenderShadowAdditiveComposition_Rtt_${route}_MSAA${samples} PROPERTIES
					TIMEOUT 900 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			endforeach()
		endforeach()
		add_executable(CoinRenderMsaaPeelingTest CoinRenderMsaaPeelingTest.cpp)
		set_target_properties(CoinRenderMsaaPeelingTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaPeelingTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaPeelingTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(samples 1 2 4)
			foreach(mode offscreen coverage sloped-depth sloped-depth-state opaque-sloped-oracle opaque-slope-rtt-staged opaque-slope-rtt-direct shadows rejections rtt-staged rtt-direct)
				add_test(NAME CoinRenderMsaaPeeling_${mode}_${samples}
					COMMAND CoinRenderMsaaPeelingTest --${mode} --samples=${samples})
				set_tests_properties(CoinRenderMsaaPeeling_${mode}_${samples} PROPERTIES TIMEOUT 600 RESOURCE_LOCK coinrender_gpu)
			endforeach()
		endforeach()
		add_test(NAME CoinRenderMsaaPeeling_CPU COMMAND CoinRenderMsaaPeelingTest --cpu --samples=1)
		set_tests_properties(CoinRenderMsaaPeeling_CPU PROPERTIES TIMEOUT 600)
		add_executable(CoinRenderMsaaPeelingWindowTest CoinRenderMsaaPeelingWindowTest.cpp)
		add_executable(CoinRenderMsaaPeelingRttTest CoinRenderMsaaPeelingRttTest.cpp)
		add_executable(CoinRenderMsaaPeelingStateTest CoinRenderMsaaPeelingStateTest.cpp)
		add_executable(CoinRenderMsaaPeelingLayersTest CoinRenderMsaaPeelingLayersTest.cpp)
		set_target_properties(CoinRenderMsaaPeelingLayersTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaPeelingLayersTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaPeelingLayersTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaPeelingLayersTest COMMAND CoinRenderMsaaPeelingLayersTest --layers)
		set_tests_properties(CoinRenderMsaaPeelingLayersTest PROPERTIES TIMEOUT 600 RESOURCE_LOCK coinrender_gpu)
		set_target_properties(CoinRenderMsaaPeelingStateTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaPeelingStateTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaPeelingStateTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaPeelingStateTest COMMAND CoinRenderMsaaPeelingStateTest --state)
		set_tests_properties(CoinRenderMsaaPeelingStateTest PROPERTIES TIMEOUT 600 RESOURCE_LOCK coinrender_gpu)
		set_target_properties(CoinRenderMsaaPeelingRttTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaPeelingRttTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaPeelingRttTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaPeelingRttTest COMMAND CoinRenderMsaaPeelingRttTest --rtt)
		set_tests_properties(CoinRenderMsaaPeelingRttTest PROPERTIES TIMEOUT 600 RESOURCE_LOCK coinrender_gpu)
		set_target_properties(CoinRenderMsaaPeelingWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaPeelingWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderMsaaPeelingWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(mode window window-background opaque-slope-window opaque-slope-window-state window-readback-atomic)
			add_test(NAME CoinRenderMsaaPeeling_${mode} COMMAND CoinRenderMsaaPeelingWindowTest --${mode})
			set_tests_properties(CoinRenderMsaaPeeling_${mode} PROPERTIES TIMEOUT 600 RESOURCE_LOCK coinrender_gpu)
			if(mode STREQUAL "window-readback-atomic")
				set_tests_properties(CoinRenderMsaaPeeling_${mode} PROPERTIES SKIP_RETURN_CODE 77)
			endif()
		endforeach()
		set_target_properties(CoinRenderMsaaWeightedTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaWeightedTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaWeightedTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(mode offscreen texture depth post shadows layers rejections)
			add_test(NAME CoinRenderMsaaWeighted_${mode} COMMAND CoinRenderMsaaWeightedTest --${mode})
			set_tests_properties(CoinRenderMsaaWeighted_${mode} PROPERTIES TIMEOUT 600 RESOURCE_LOCK coinrender_gpu)
		endforeach()
		add_executable(CoinRenderMsaaWeightedRttTest CoinRenderMsaaWeightedRttTest.cpp)
		set_target_properties(CoinRenderMsaaWeightedRttTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaWeightedRttTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaWeightedRttTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaWeightedRttTest COMMAND CoinRenderMsaaWeightedRttTest --rtt)
		set_tests_properties(CoinRenderMsaaWeightedRttTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderMsaaWeightedWindowTest CoinRenderMsaaWeightedWindowTest.cpp)
		set_target_properties(CoinRenderMsaaWeightedWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaWeightedWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderMsaaWeightedWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaWeightedWindowTest COMMAND CoinRenderMsaaWeightedWindowTest --window)
		set_tests_properties(CoinRenderMsaaWeightedWindowTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderMsaaWeightedWindowBackgroundTest COMMAND CoinRenderMsaaWeightedWindowTest --window-background)
		set_tests_properties(CoinRenderMsaaWeightedWindowBackgroundTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderMsaaShadowTest CoinRenderMsaaShadowTest.cpp)
		set_target_properties(CoinRenderMsaaShadowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaShadowTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaShadowTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(mode offscreen slots blend layers rejections state)
			add_test(NAME CoinRenderMsaaShadow_${mode} COMMAND CoinRenderMsaaShadowTest --${mode})
			set_tests_properties(CoinRenderMsaaShadow_${mode} PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu)
		endforeach()
		add_executable(CoinRenderMsaaShadowRttTest CoinRenderMsaaShadowRttTest.cpp)
		set_target_properties(CoinRenderMsaaShadowRttTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaShadowRttTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaShadowRttTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaShadowRttTest COMMAND CoinRenderMsaaShadowRttTest --rtt)
		set_tests_properties(CoinRenderMsaaShadowRttTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderShadowSpatialWindowTest CoinRenderShadowSpatialWindowTest.cpp)
		target_sources(CoinRenderShadowSpatialWindowTest PRIVATE ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		target_compile_definitions(CoinRenderShadowSpatialWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderShadowSpatialWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderShadowSpatialWindowTest PRIVATE ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderShadowSpatialWindowTest COMMAND CoinRenderShadowSpatialWindowTest)
		add_test(NAME CoinRenderShadowSpatialWindow2Test COMMAND CoinRenderShadowSpatialWindowTest --samples 2)
		add_test(NAME CoinRenderShadowSpatialWindow4PortableTest COMMAND CoinRenderShadowSpatialWindowTest --samples 4 --portable-sampling)
		add_test(NAME CoinRenderShadowSpatialWindowRttTest COMMAND CoinRenderShadowSpatialWindowTest --rtt --direct)
		set_tests_properties(CoinRenderShadowSpatialWindow2Test CoinRenderShadowSpatialWindow4PortableTest CoinRenderShadowSpatialWindowRttTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")

		set_tests_properties(CoinRenderShadowSpatialWindowTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu SKIP_RETURN_CODE 77 ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		add_executable(CoinRenderInheritedSurfaceWindowTest CoinRenderInheritedSurfaceWindowTest.cpp)
		target_sources(CoinRenderInheritedSurfaceWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		target_compile_definitions(CoinRenderInheritedSurfaceWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderInheritedSurfaceWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderInheritedSurfaceWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(inherited_configuration RANGE 0 5)
			add_test(NAME CoinRenderInheritedSurfaceWindow_${inherited_configuration}
				COMMAND CoinRenderInheritedSurfaceWindowTest --configuration ${inherited_configuration})
			set_tests_properties(CoinRenderInheritedSurfaceWindow_${inherited_configuration}
				PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endforeach()
		add_executable(CoinRenderShadowMultitextureWindowTest CoinRenderShadowMultitextureWindowTest.cpp)
		target_sources(CoinRenderShadowMultitextureWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderShadowMultitextureWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderShadowMultitextureWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderShadowMultitextureWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			find_path(COIN_TEST_XCB_INCLUDE_DIR NAMES xcb/xcbext.h)
		endif()
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND COIN_TEST_XCB_INCLUDE_DIR)
			add_library(CoinRenderXfixesReplyAudit MODULE CoinRenderXfixesReplyAudit.c)
			target_include_directories(CoinRenderXfixesReplyAudit PRIVATE ${COIN_TEST_XCB_INCLUDE_DIR})
			set_target_properties(CoinRenderXfixesReplyAudit PROPERTIES PREFIX "")
			add_executable(CoinRenderX11WsiVersionRefusalTest CoinRenderX11WsiVersionRefusalTest.cpp)
			target_sources(CoinRenderX11WsiVersionRefusalTest PRIVATE
				${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
				${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
			set_target_properties(CoinRenderX11WsiVersionRefusalTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(CoinRenderX11WsiVersionRefusalTest CoinRender Coin ${X11_LIBRARIES})
			target_include_directories(CoinRenderX11WsiVersionRefusalTest PRIVATE
				${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
				${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
			foreach(version 1.0 6.1)
				add_test(NAME CoinRenderX11WsiVersionRefusal_${version}
					COMMAND ${CMAKE_COMMAND} -E env
					"LD_AUDIT=$<TARGET_FILE:CoinRenderXfixesReplyAudit>" "COIN_TEST_XFIXES_REPLY=${version}"
					$<TARGET_FILE:CoinRenderX11WsiVersionRefusalTest> --expect-wsi-refusal --injected-version ${version})
				set_tests_properties(CoinRenderX11WsiVersionRefusal_${version} PROPERTIES
					TIMEOUT 90 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "WGPU_BACKEND=vulkan;COIN_BGFX_RENDERER=vulkan")
			endforeach()
		endif()
		# Explicit server-capability fixture: run with --expect-wsi-refusal on a
		# real X server configured with -extension XFIXES. Do not negotiate in test.
		add_executable(CoinRenderX11WsiRefusalTest CoinRenderX11WsiRefusalTest.cpp)
		target_sources(CoinRenderX11WsiRefusalTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderX11WsiRefusalTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderX11WsiRefusalTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderX11WsiRefusalTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_executable(CoinRenderX11ConnectionLifetimeTest CoinRenderX11ConnectionLifetimeTest.cpp)
		target_sources(CoinRenderX11ConnectionLifetimeTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderX11ConnectionLifetimeTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderX11ConnectionLifetimeTest CoinRender Coin ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
		target_include_directories(CoinRenderX11ConnectionLifetimeTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(samples 1 4)
			add_test(NAME CoinRenderX11ConnectionLifetime_MSAA${samples}
				COMMAND CoinRenderX11ConnectionLifetimeTest --samples ${samples})
			add_test(NAME CoinRenderX11ConnectionReuse_MSAA${samples}
				COMMAND CoinRenderX11ConnectionLifetimeTest --samples ${samples} --require-reuse)
			set_tests_properties(CoinRenderX11ConnectionLifetime_MSAA${samples} CoinRenderX11ConnectionReuse_MSAA${samples}
				PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
				ENVIRONMENT "WGPU_BACKEND=vulkan;COIN_BGFX_RENDERER=vulkan")
		endforeach()
		add_test(NAME CoinRenderX11ConnectionReuse_Adversarial
			COMMAND CoinRenderX11ConnectionLifetimeTest --samples 2 --require-reuse --adversarial --portable-sampling)
		set_tests_properties(CoinRenderX11ConnectionReuse_Adversarial PROPERTIES
			TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "WGPU_BACKEND=vulkan;COIN_BGFX_RENDERER=vulkan")
		add_executable(CoinRenderShadowMultitextureAlphaTest CoinRenderShadowMultitextureAlphaTest.cpp)
		target_sources(CoinRenderShadowMultitextureAlphaTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderShadowMultitextureAlphaTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderShadowMultitextureAlphaTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderShadowMultitextureAlphaTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_executable(CoinRenderShadowRttDepthTest CoinRenderShadowRttDepthTest.cpp)
		target_sources(CoinRenderShadowRttDepthTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderShadowRttDepthTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderShadowRttDepthTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderShadowRttDepthTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderShadowRttDepth_CapturePayload COMMAND CoinRenderShadowRttDepthTest --capture-payload)
		set_tests_properties(CoinRenderShadowRttDepth_CapturePayload PROPERTIES
			LABELS "coinrender;shadow;rtt;cpu" ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		add_test(NAME CoinRenderShadowRttDepth_ObjectModes COMMAND CoinRenderShadowRttDepthTest --object-depth-modes --samples 4 --rtt-direct)
		set_tests_properties(CoinRenderShadowRttDepth_ObjectModes PROPERTIES
			LABELS "coinrender;shadow;rtt;depth;gpu" SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		add_test(NAME CoinRenderShadowRttDepth_Atomic COMMAND CoinRenderShadowRttDepthTest --atomic --samples 4 --rtt-staged)
		set_tests_properties(CoinRenderShadowRttDepth_Atomic PROPERTIES
			LABELS "coinrender;shadow;rtt;depth;gpu" SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		add_test(NAME CoinRenderShadowRttDepth_HeterogeneousBudget COMMAND CoinRenderShadowRttDepthTest --heterogeneous-budget --samples 4 --rtt-direct)
		set_tests_properties(CoinRenderShadowRttDepth_HeterogeneousBudget PROPERTIES
			LABELS "coinrender;shadow;rtt;depth;gpu" SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		add_test(NAME CoinRenderShadowRttDepth_Groups COMMAND CoinRenderShadowRttDepthTest --groups --samples 4 --rtt-direct)
		set_tests_properties(CoinRenderShadowRttDepth_Groups PROPERTIES
			TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")

		foreach(depth_mode object peeling weighted)
			add_test(NAME CoinRenderShadowRttDepth_Depth_${depth_mode}
				COMMAND CoinRenderShadowRttDepthTest --depth --samples 4 --mechanism ${depth_mode} --rtt-direct)
			set_tests_properties(CoinRenderShadowRttDepth_Depth_${depth_mode} PROPERTIES
				TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		endforeach()

		foreach(route staged direct)
			add_test(NAME CoinRenderShadowRttDepth_Multi_${route}
				COMMAND CoinRenderShadowRttDepthTest --multi-rtt --samples 4 --rtt-${route})
			set_tests_properties(CoinRenderShadowRttDepth_Multi_${route} PROPERTIES
				TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		endforeach()

		foreach(route staged direct)
			add_test(NAME CoinRenderShadowMultitextureAlpha_ViewPressure_${route}
				COMMAND CoinRenderShadowMultitextureAlphaTest --view-pressure --samples 4 --rtt-${route})
			set_tests_properties(CoinRenderShadowMultitextureAlpha_ViewPressure_${route} PROPERTIES
				TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		endforeach()

		add_test(NAME CoinRenderShadowMultitextureAlpha_SixMaps_MSAA4_Portable
			COMMAND CoinRenderShadowMultitextureAlphaTest --map-domain --maps 6 --samples 4 --portable-sampling)
		set_tests_properties(CoinRenderShadowMultitextureAlpha_SixMaps_MSAA4_Portable PROPERTIES
			TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")

		add_test(NAME CoinRenderShadowMultitextureAlpha_MapGroups_MSAA4_Portable
			COMMAND CoinRenderShadowMultitextureAlphaTest --map-groups --samples 4 --portable-sampling)
		set_tests_properties(CoinRenderShadowMultitextureAlpha_MapGroups_MSAA4_Portable PROPERTIES
			TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")

		add_test(NAME CoinRenderShadowMultitextureAlpha_Stipple_MSAA4_Portable
			COMMAND CoinRenderShadowMultitextureAlphaTest --stipple --samples 4 --portable-sampling)
		set_tests_properties(CoinRenderShadowMultitextureAlpha_Stipple_MSAA4_Portable PROPERTIES
			TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")

		foreach(alpha_mode object peeling weighted)
			add_test(NAME CoinRenderShadowMultitextureAlpha_${alpha_mode} COMMAND CoinRenderShadowMultitextureAlphaTest --smoke --mechanism ${alpha_mode})
			set_tests_properties(CoinRenderShadowMultitextureAlpha_${alpha_mode} PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endforeach()

		foreach(alpha_mode object peeling weighted)
			foreach(samples 2 4)
				set(alpha_args --campaign --mechanism ${alpha_mode} --samples ${samples})
				set(alpha_precise 1)
				if(samples EQUAL 4)
					list(APPEND alpha_args --portable-sampling)
					set(alpha_precise 0)
				endif()
				add_test(NAME CoinRenderShadowMultitextureAlpha_${alpha_mode}_MSAA${samples} COMMAND CoinRenderShadowMultitextureAlphaTest ${alpha_args})
				set_tests_properties(CoinRenderShadowMultitextureAlpha_${alpha_mode}_MSAA${samples} PROPERTIES
					TIMEOUT 900 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=${alpha_precise}")
			endforeach()
			foreach(route staged direct)
				add_test(NAME CoinRenderShadowMultitextureAlpha_${alpha_mode}_Rtt_${route}
					COMMAND CoinRenderShadowMultitextureAlphaTest --campaign --mechanism ${alpha_mode} --samples 2 --rtt-${route})
				set_tests_properties(CoinRenderShadowMultitextureAlpha_${alpha_mode}_Rtt_${route} PROPERTIES
					TIMEOUT 900 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
			endforeach()
		endforeach()

		foreach(shape Fan Strip)
			if(shape STREQUAL "Fan")
				set(shadow_window_precise 1)
			else()
				set(shadow_window_precise 0)
			endif()
			foreach(policy Native Portable)
				set(shadow_window_args --campaign)
				if(policy STREQUAL "Portable")
					list(APPEND shadow_window_args --portable-sampling)
				endif()
				add_test(NAME CoinRenderShadowMultitextureWindow_${shape}_${policy}
					COMMAND CoinRenderShadowMultitextureWindowTest ${shadow_window_args})
				set_tests_properties(CoinRenderShadowMultitextureWindow_${shape}_${policy} PROPERTIES
					TIMEOUT 1200 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=${shadow_window_precise}")
			endforeach()
		endforeach()
		foreach(samples 2 4)
			foreach(shape Fan Strip)
				if(shape STREQUAL "Fan")
					set(shadow_msaa_precise 1)
				else()
					set(shadow_msaa_precise 0)
				endif()
				foreach(policy Native Portable)
					set(shadow_msaa_args --campaign --samples ${samples})
					if(policy STREQUAL "Portable")
						list(APPEND shadow_msaa_args --portable-sampling)
					endif()
					add_test(NAME CoinRenderShadowMultitextureWindow_MSAA${samples}_${shape}_${policy}
						COMMAND CoinRenderShadowMultitextureWindowTest ${shadow_msaa_args})
					set_tests_properties(CoinRenderShadowMultitextureWindow_MSAA${samples}_${shape}_${policy} PROPERTIES
						TIMEOUT 1200 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
						ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=${shadow_msaa_precise}")
				endforeach()
				add_test(NAME CoinRenderShadowMultitextureWindow_MSAA${samples}_${shape}_DepthOracle
					COMMAND CoinRenderShadowMultitextureWindowTest --depth-oracle --samples ${samples})
				set_tests_properties(CoinRenderShadowMultitextureWindow_MSAA${samples}_${shape}_DepthOracle PROPERTIES
					TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=${shadow_msaa_precise}")
			endforeach()
			add_test(NAME CoinRenderShadowMultitextureWindow_MSAA${samples}_Lifecycle
				COMMAND CoinRenderShadowMultitextureWindowTest --lifecycle --samples ${samples})
			set_tests_properties(CoinRenderShadowMultitextureWindow_MSAA${samples}_Lifecycle PROPERTIES
				TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		endforeach()
		foreach(samples 2 4)
			foreach(shape Fan Strip)
				if(shape STREQUAL "Fan")
					set(custom_depth_precise 1)
				else()
					set(custom_depth_precise 0)
				endif()
				foreach(policy Native Portable)
					set(custom_depth_args --samples ${samples})
					if(policy STREQUAL "Portable")
						list(APPEND custom_depth_args --portable-sampling)
					endif()
					foreach(mode custom-depth custom-depth-oracle)
						add_test(NAME CoinRenderShadowMultitextureWindow_${mode}_MSAA${samples}_${shape}_${policy}
							COMMAND CoinRenderShadowMultitextureWindowTest --${mode} ${custom_depth_args})
						set_tests_properties(CoinRenderShadowMultitextureWindow_${mode}_MSAA${samples}_${shape}_${policy} PROPERTIES
							TIMEOUT 1200 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
							ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=${custom_depth_precise}")
					endforeach()
				endforeach()
			endforeach()
		endforeach()
		add_test(NAME CoinRenderShadowMultitextureWindow_CustomDepthBatchSmoke
			COMMAND CoinRenderShadowMultitextureWindowTest --custom-depth-batch-smoke)
		set_tests_properties(CoinRenderShadowMultitextureWindow_CustomDepthBatchSmoke PROPERTIES
			TIMEOUT 1200 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderShadowMultitextureWindow_CustomDepthSharedDisplayBatch
			COMMAND CoinRenderShadowMultitextureWindowTest --custom-depth-shared-display-batch)
		set_tests_properties(CoinRenderShadowMultitextureWindow_CustomDepthSharedDisplayBatch PROPERTIES
			TIMEOUT 1200 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		foreach(samples 2 4)
			foreach(shape Fan Strip)
				if(shape STREQUAL "Fan")
					set(custom_boundaries_precise 1)
				else()
					set(custom_boundaries_precise 0)
				endif()
				add_test(NAME CoinRenderShadowMultitextureWindow_CustomDepthBoundaries_MSAA${samples}_${shape}
					COMMAND CoinRenderShadowMultitextureWindowTest --custom-depth-boundaries --samples ${samples})
				set_tests_properties(CoinRenderShadowMultitextureWindow_CustomDepthBoundaries_MSAA${samples}_${shape} PROPERTIES
					TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=${custom_boundaries_precise}")
			endforeach()
		endforeach()
		add_test(NAME CoinRenderShadowMultitextureWindowLifecycle COMMAND CoinRenderShadowMultitextureWindowTest --lifecycle)
		set_tests_properties(CoinRenderShadowMultitextureWindowLifecycle PROPERTIES TIMEOUT 300
			SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
		add_executable(CoinRenderMsaaShadowWindowTest CoinRenderMsaaShadowWindowTest.cpp)
		set_target_properties(CoinRenderMsaaShadowWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaShadowWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderMsaaShadowWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaShadowWindowTest COMMAND CoinRenderMsaaShadowWindowTest --window)
		set_tests_properties(CoinRenderMsaaShadowWindowTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderMsaaShadowWindowLiveRuntimeTest COMMAND CoinRenderMsaaShadowWindowTest --window-live-runtime)
		set_tests_properties(CoinRenderMsaaShadowWindowLiveRuntimeTest PROPERTIES TIMEOUT 300 RESOURCE_LOCK coinrender_gpu)
		set_target_properties(CoinRenderMsaaTransparencyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaTransparencyTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaTransparencyTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(mode cpu-1x offscreen layers triangle-sorting highlight rejections)
			add_test(NAME CoinRenderMsaaTransparency_${mode} COMMAND CoinRenderMsaaTransparencyTest --${mode})
			set_tests_properties(CoinRenderMsaaTransparency_${mode} PROPERTIES TIMEOUT 240 RESOURCE_LOCK coinrender_gpu)
		endforeach()
		set_tests_properties(CoinRenderMsaaTransparency_highlight PROPERTIES
			ENVIRONMENT "COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE=0")
		add_test(NAME CoinRenderMsaaTransparency_highlight_separate
			COMMAND CoinRenderMsaaTransparencyTest --highlight)
		set_tests_properties(CoinRenderMsaaTransparency_highlight_separate PROPERTIES
			TIMEOUT 240 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE=1")
		add_executable(CoinRenderMsaaTransparencyRttTest CoinRenderMsaaTransparencyRttTest.cpp)
		set_target_properties(CoinRenderMsaaTransparencyRttTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaTransparencyRttTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaTransparencyRttTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaTransparencyRttTest COMMAND CoinRenderMsaaTransparencyRttTest --rtt)
		set_tests_properties(CoinRenderMsaaTransparencyRttTest PROPERTIES TIMEOUT 240 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderMsaaTransparencyWindowTest CoinRenderMsaaTransparencyWindowTest.cpp)
		set_target_properties(CoinRenderMsaaTransparencyWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaTransparencyWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderMsaaTransparencyWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaTransparencyWindowTest COMMAND CoinRenderMsaaTransparencyWindowTest --window)
		set_tests_properties(CoinRenderMsaaTransparencyWindowTest PROPERTIES TIMEOUT 240 RESOURCE_LOCK coinrender_gpu)
		set_target_properties(CoinRenderMsaaLayersTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaLayersTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaLayersTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaLayersOffscreenTest COMMAND CoinRenderMsaaLayersTest --offscreen)
		set_tests_properties(CoinRenderMsaaLayersOffscreenTest PROPERTIES TIMEOUT 180 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderMsaaLayersCpuCaptureTest COMMAND CoinRenderMsaaLayersTest --cpu-1x)
		set_tests_properties(CoinRenderMsaaLayersCpuCaptureTest PROPERTIES TIMEOUT 120)
		add_test(NAME CoinRenderMsaaLayersHighlightTest COMMAND CoinRenderMsaaLayersTest --manager-highlight)
		set_tests_properties(CoinRenderMsaaLayersHighlightTest PROPERTIES TIMEOUT 180 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderMsaaLayersRejectionsTest COMMAND CoinRenderMsaaLayersTest --rejections)
		set_tests_properties(CoinRenderMsaaLayersRejectionsTest PROPERTIES TIMEOUT 180 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderMsaaLayersRttTest CoinRenderMsaaLayersRttTest.cpp)
		set_target_properties(CoinRenderMsaaLayersRttTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaLayersRttTest CoinRender Coin)
		target_include_directories(CoinRenderMsaaLayersRttTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaLayersRttTest COMMAND CoinRenderMsaaLayersRttTest --rtt)
		set_tests_properties(CoinRenderMsaaLayersRttTest PROPERTIES TIMEOUT 180 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderMsaaLayersWindowTest CoinRenderMsaaLayersWindowTest.cpp)
		set_target_properties(CoinRenderMsaaLayersWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaLayersWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderMsaaLayersWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaLayersWindowTest COMMAND CoinRenderMsaaLayersWindowTest --window)
		set_tests_properties(CoinRenderMsaaLayersWindowTest PROPERTIES TIMEOUT 180 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderManagerHighlightTest CoinRenderManagerHighlightTest.cpp)
		add_executable(CoinRenderBgfxOpenGLLifetimeTest CoinRenderBgfxOpenGLLifetimeTest.cpp)
		set_target_properties(CoinRenderBgfxOpenGLLifetimeTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderBgfxOpenGLLifetimeTest CoinRender Coin ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
		target_include_directories(CoinRenderBgfxOpenGLLifetimeTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderBgfxOpenGLLifetimeTest COMMAND CoinRenderBgfxOpenGLLifetimeTest)
		set_tests_properties(CoinRenderBgfxOpenGLLifetimeTest PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		# Bounded probes separate fresh-connection recreation, retained native
		# handles, sample transitions, thread exit and caller EGL ownership.
		foreach(samples 2 4)
			add_test(NAME CoinRenderBgfxOpenGLFreshSamples${samples}Test
				COMMAND CoinRenderBgfxOpenGLLifetimeTest --samples ${samples} --cycles 4 --distinct-frames --resize)
			set_tests_properties(CoinRenderBgfxOpenGLFreshSamples${samples}Test PROPERTIES
				TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		endforeach()
		add_test(NAME CoinRenderBgfxOpenGLSameDisplayTest
			COMMAND CoinRenderBgfxOpenGLLifetimeTest --same-display --cycles 4 --distinct-frames --resize)
		add_test(NAME CoinRenderBgfxOpenGLSamplesTransitionTest
			COMMAND CoinRenderBgfxOpenGLLifetimeTest --same-window --transition-2-to-4 --cycles 4 --distinct-frames --resize)
		add_test(NAME CoinRenderBgfxOpenGLWorkerLifetimeTest
			COMMAND CoinRenderBgfxOpenGLLifetimeTest --worker --cycles 4 --distinct-frames)
		add_test(NAME CoinRenderBgfxOpenGLHostEglTest
			COMMAND CoinRenderBgfxOpenGLLifetimeTest --host-egl --cycles 4 --distinct-frames --resize)
		add_test(NAME CoinRenderBgfxOpenGLHostEglSameDisplayTest
			COMMAND CoinRenderBgfxOpenGLLifetimeTest --host-egl-same-display --cycles 4 --distinct-frames --resize)
		set_tests_properties(CoinRenderBgfxOpenGLSameDisplayTest CoinRenderBgfxOpenGLSamplesTransitionTest
			CoinRenderBgfxOpenGLWorkerLifetimeTest CoinRenderBgfxOpenGLHostEglTest
			CoinRenderBgfxOpenGLHostEglSameDisplayTest PROPERTIES
			TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_executable(CoinRenderNativeEglLifetimeTest CoinRenderNativeEglLifetimeTest.cpp)
		target_link_libraries(CoinRenderNativeEglLifetimeTest ${X11_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
		target_include_directories(CoinRenderNativeEglLifetimeTest PRIVATE ${X11_INCLUDE_DIR})
		add_test(NAME CoinRenderNativeEglLifetimeTest COMMAND CoinRenderNativeEglLifetimeTest --worker)
		add_test(NAME CoinRenderNativeEglSameDisplayTest COMMAND CoinRenderNativeEglLifetimeTest --worker --same-display)
		add_test(NAME CoinRenderNativeEglIsolatedRuntimeDisplayTest
			COMMAND CoinRenderNativeEglLifetimeTest --worker --isolated-runtime-display)
		add_test(NAME CoinRenderNativeEglPlatformX11IsolatedRuntimeDisplayTest
			COMMAND CoinRenderNativeEglLifetimeTest --worker --isolated-runtime-display --platform-x11)
		add_test(NAME CoinRenderNativeEglSharedWindowIsolatedRuntimeDisplayTest
			COMMAND CoinRenderNativeEglLifetimeTest --worker --isolated-runtime-display --shared-window-context)
		add_test(NAME CoinRenderNativeEglPlatformX11SharedWindowIsolatedRuntimeDisplayTest
			COMMAND CoinRenderNativeEglLifetimeTest --worker --isolated-runtime-display --shared-window-context --platform-x11)
		add_test(NAME CoinRenderNativeEglHostOwnerWorkerTest
			COMMAND CoinRenderNativeEglLifetimeTest --worker --isolated-runtime-display --host-egl)
		set_tests_properties(CoinRenderNativeEglLifetimeTest CoinRenderNativeEglSameDisplayTest
			CoinRenderNativeEglIsolatedRuntimeDisplayTest CoinRenderNativeEglPlatformX11IsolatedRuntimeDisplayTest
			CoinRenderNativeEglSharedWindowIsolatedRuntimeDisplayTest
			CoinRenderNativeEglPlatformX11SharedWindowIsolatedRuntimeDisplayTest
			CoinRenderNativeEglHostOwnerWorkerTest PROPERTIES
			TIMEOUT 300 RESOURCE_LOCK coinrender_gpu)

		set_target_properties(CoinRenderManagerHighlightTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderManagerHighlightTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderManagerHighlightTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderManagerHighlightCpuTest COMMAND CoinRenderManagerHighlightTest --cpu)
		set_tests_properties(CoinRenderManagerHighlightCpuTest PROPERTIES TIMEOUT 60)
		add_test(NAME CoinRenderManagerHighlightGpuTest COMMAND CoinRenderManagerHighlightTest --gpu)
		set_tests_properties(CoinRenderManagerHighlightGpuTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderManagerHighlightWindowTest COMMAND CoinRenderManagerHighlightTest --window)
		set_tests_properties(CoinRenderManagerHighlightWindowTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderManagerHighlightPathsTest CoinRenderManagerHighlightPathsTest.cpp)
		set_target_properties(CoinRenderManagerHighlightPathsTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderManagerHighlightPathsTest CoinRender Coin)
		target_include_directories(CoinRenderManagerHighlightPathsTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderManagerHighlightPathsCpuTest COMMAND CoinRenderManagerHighlightPathsTest --cpu)
		set_tests_properties(CoinRenderManagerHighlightPathsCpuTest PROPERTIES TIMEOUT 60)
		add_test(NAME CoinRenderManagerHighlightPathsGpuTest COMMAND CoinRenderManagerHighlightPathsTest --gpu)
		set_tests_properties(CoinRenderManagerHighlightPathsGpuTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderManagerHighlightTraversalTest CoinRenderManagerHighlightTraversalTest.cpp)
		set_target_properties(CoinRenderManagerHighlightTraversalTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderManagerHighlightTraversalTest CoinRender Coin)
		target_include_directories(CoinRenderManagerHighlightTraversalTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderManagerHighlightTraversalCpuTest COMMAND CoinRenderManagerHighlightTraversalTest --cpu)
		set_tests_properties(CoinRenderManagerHighlightTraversalCpuTest PROPERTIES TIMEOUT 60)
		add_test(NAME CoinRenderManagerHighlightTraversalGpuTest COMMAND CoinRenderManagerHighlightTraversalTest --gpu)
		set_tests_properties(CoinRenderManagerHighlightTraversalGpuTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderManagerHighlightTraversalNativeTest COMMAND CoinRenderManagerHighlightTraversalTest --native)
		set_tests_properties(CoinRenderManagerHighlightTraversalNativeTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		add_executable(CoinRenderManagerHighlightCompositionTest CoinRenderManagerHighlightCompositionTest.cpp)
		set_target_properties(CoinRenderManagerHighlightCompositionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderManagerHighlightCompositionTest CoinRender Coin)
		target_include_directories(CoinRenderManagerHighlightCompositionTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderManagerHighlightCompositionCpuTest COMMAND CoinRenderManagerHighlightCompositionTest --cpu)
		set_tests_properties(CoinRenderManagerHighlightCompositionCpuTest PROPERTIES TIMEOUT 60 ENVIRONMENT "COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE=0")
		add_test(NAME CoinRenderManagerHighlightCompositionCpuSeparateOverrideTest COMMAND CoinRenderManagerHighlightCompositionTest --cpu)
		set_tests_properties(CoinRenderManagerHighlightCompositionCpuSeparateOverrideTest PROPERTIES TIMEOUT 60 ENVIRONMENT "COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE=1")
		add_test(NAME CoinRenderManagerHighlightCompositionGpuTest COMMAND CoinRenderManagerHighlightCompositionTest --gpu)
		set_tests_properties(CoinRenderManagerHighlightCompositionGpuTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE=0")
		add_test(NAME CoinRenderManagerHighlightCompositionGpuSeparateOverrideTest COMMAND CoinRenderManagerHighlightCompositionTest --gpu)
		set_tests_properties(CoinRenderManagerHighlightCompositionGpuSeparateOverrideTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE=1")
		add_test(NAME CoinRenderManagerHighlightCompositionNativeTest COMMAND CoinRenderManagerHighlightCompositionTest --native)
		set_tests_properties(CoinRenderManagerHighlightCompositionNativeTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE=0;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		add_test(NAME CoinRenderManagerHighlightCompositionNativeSeparateOverrideTest COMMAND CoinRenderManagerHighlightCompositionTest --native)
		set_tests_properties(CoinRenderManagerHighlightCompositionNativeSeparateOverrideTest PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		set_target_properties(CoinRenderManagerWindowLayersTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderManagerWindowLayersTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderManagerWindowLayersTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderManagerWindowLayersTest COMMAND CoinRenderManagerWindowLayersTest)
		set_tests_properties(CoinRenderManagerWindowLayersTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderManagerWindowStereoTest CoinRenderManagerWindowStereoTest.cpp)
		set_target_properties(CoinRenderManagerWindowStereoTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderManagerWindowStereoTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderManagerWindowStereoTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderManagerWindowStereoTest COMMAND CoinRenderManagerWindowStereoTest)
		set_tests_properties(CoinRenderManagerWindowStereoTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderFirstPresentTest CoinRenderFirstPresentTest.cpp)
		set_target_properties(CoinRenderFirstPresentTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderFirstPresentTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderFirstPresentTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderTerrainFirstPresentVulkanTest COMMAND CoinRenderFirstPresentTest --terrain --managed)
		add_test(NAME CoinRenderTerrainFirstPresentOpenGLTest COMMAND CoinRenderFirstPresentTest --terrain --managed)
		set_tests_properties(CoinRenderTerrainFirstPresentVulkanTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "WGPU_BACKEND=vulkan;COIN_BGFX_RENDERER=vulkan")
		set_tests_properties(CoinRenderTerrainFirstPresentOpenGLTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "WGPU_BACKEND=gl;COIN_BGFX_RENDERER=opengl")
		add_test(NAME CoinRenderFirstPresentVulkanTest COMMAND CoinRenderFirstPresentTest)
		add_test(NAME CoinRenderFirstPresentOpenGLTest COMMAND CoinRenderFirstPresentTest)
		set_tests_properties(CoinRenderFirstPresentVulkanTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "WGPU_BACKEND=vulkan;COIN_BGFX_RENDERER=vulkan")
		set_tests_properties(CoinRenderFirstPresentOpenGLTest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
			ENVIRONMENT "WGPU_BACKEND=gl;COIN_BGFX_RENDERER=opengl")
	endif()

	add_executable(CoinRenderAdvancedCoverageTest CoinRenderAdvancedCoverageTest.cpp)
	set_target_properties(CoinRenderAdvancedCoverageTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderAdvancedCoverageTest CoinRender Coin)
	target_include_directories(CoinRenderAdvancedCoverageTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	if(UNIX AND NOT APPLE AND NOT ANDROID AND X11_FOUND)
		target_compile_definitions(CoinRenderAdvancedCoverageTest PRIVATE COIN_COVERAGE_XLIB=1)
		target_link_libraries(CoinRenderAdvancedCoverageTest ${X11_LIBRARIES})
		target_include_directories(CoinRenderAdvancedCoverageTest PRIVATE ${X11_INCLUDE_DIR})
	endif()
	add_test(NAME CoinRenderAdvancedCoverageCpuTest COMMAND CoinRenderAdvancedCoverageTest --cpu)
	set_tests_properties(CoinRenderAdvancedCoverageCpuTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderCanonicalCoverageTest CoinRenderCanonicalCoverageTest.cpp)
	set_target_properties(CoinRenderCanonicalCoverageTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderCanonicalCoverageTest CoinRender Coin)
	target_include_directories(CoinRenderCanonicalCoverageTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderCanonicalCoverageCpuTest COMMAND CoinRenderCanonicalCoverageTest --cpu)
	set_tests_properties(CoinRenderCanonicalCoverageCpuTest PROPERTIES TIMEOUT 30)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderCanonicalCameraCoverageTest COMMAND ${CMAKE_COMMAND} -E env
			--unset=COIN_RENDER_REQUIRE_GL_REFERENCE COIN_RENDER_REQUIRE_CAMERA_REFERENCE=1
			COIN_RENDER_CAMERA_REFERENCE_CANONICAL_COVERAGE=1 $<TARGET_FILE:CoinRenderCameraReuseReferenceTest>)
		set_tests_properties(CoinRenderCanonicalCameraCoverageTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		if(UNIX AND NOT APPLE AND NOT ANDROID AND X11_FOUND)
			add_test(NAME CoinRenderAdvancedCoverageWindowTest COMMAND CoinRenderAdvancedCoverageTest --window)
			set_tests_properties(CoinRenderAdvancedCoverageWindowTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
		add_test(NAME CoinRenderAdvancedCoverageGpuTest COMMAND CoinRenderAdvancedCoverageTest)
		set_tests_properties(CoinRenderAdvancedCoverageGpuTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderCanonicalCoverageGpuTest COMMAND CoinRenderCanonicalCoverageTest)
		set_tests_properties(CoinRenderCanonicalCoverageGpuTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endif()

	add_executable(CoinRenderBumpRejectionTest CoinRenderBumpRejectionTest.cpp)
	set_target_properties(CoinRenderBumpRejectionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderBumpRejectionTest CoinRender Coin)
	target_include_directories(CoinRenderBumpRejectionTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderBumpRejectionTest COMMAND CoinRenderBumpRejectionTest)
	set_tests_properties(CoinRenderBumpRejectionTest PROPERTIES TIMEOUT 30)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		add_test(NAME CoinRenderBumpRejectionVulkanTest COMMAND CoinRenderBumpRejectionTest --gpu)
		add_test(NAME CoinRenderBumpRejectionOpenGLTest COMMAND CoinRenderBumpRejectionTest --gpu)
		set_tests_properties(CoinRenderBumpRejectionVulkanTest PROPERTIES
			TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_BGFX_RENDERER=vulkan")
		set_tests_properties(CoinRenderBumpRejectionOpenGLTest PROPERTIES
			TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
	elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME CoinRenderBumpRejectionGpuTest COMMAND CoinRenderBumpRejectionTest --gpu)
		set_tests_properties(CoinRenderBumpRejectionGpuTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endif()
	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE" AND UNIX AND NOT APPLE AND NOT ANDROID AND X11_FOUND)
		add_executable(CoinRenderMsaaWindowTest CoinRenderMsaaWindowTest.cpp)
		set_target_properties(CoinRenderMsaaWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderMsaaWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderMsaaWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderMsaaWindowTest COMMAND CoinRenderMsaaWindowTest)
		set_tests_properties(CoinRenderMsaaWindowTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_executable(CoinRenderBumpWindowTest CoinRenderBumpWindowTest.cpp)
		set_target_properties(CoinRenderBumpWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderBumpWindowTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderBumpWindowTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderBumpWindowTest COMMAND CoinRenderBumpWindowTest)
		set_tests_properties(CoinRenderBumpWindowTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
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
	add_executable(CoinRenderVolumeNpotRttTest CoinRenderVolumeNpotRttTest.cpp)
	target_compile_definitions(CoinRenderVolumeNpotRttTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderVolumeNpotRttTest CoinRender Coin)
	target_include_directories(CoinRenderVolumeNpotRttTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(npot_route staged direct)
		add_test(NAME CoinRenderVolumeNpotRtt_${npot_route} COMMAND CoinRenderVolumeNpotRttTest --${npot_route})
		add_test(NAME CoinRenderVolumeNpotRttPortable_${npot_route} COMMAND CoinRenderVolumeNpotRttTest --${npot_route} --portable-sampling)
		set_tests_properties(CoinRenderVolumeNpotRtt_${npot_route} CoinRenderVolumeNpotRttPortable_${npot_route} PROPERTIES TIMEOUT 180 RESOURCE_LOCK coinrender_gpu)
	endforeach()
	add_executable(CoinRenderImageInheritedRttTest CoinRenderImageInheritedRttTest.cpp)
	set_target_properties(CoinRenderImageInheritedRttTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderImageInheritedRttTest CoinRender Coin)
	target_include_directories(CoinRenderImageInheritedRttTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	if(X11_FOUND)
		add_executable(CoinRenderImageNormalTexgenGpuTest CoinRenderImageNormalTexgenGpuTest.cpp)
		target_sources(CoinRenderImageNormalTexgenGpuTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderImageNormalTexgenGpuTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderImageNormalTexgenGpuTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderImageNormalTexgenGpuTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderImageNormalTexgenGpu COMMAND CoinRenderImageNormalTexgenGpuTest --gpu)
		add_test(NAME CoinRenderImageNormalTexgenCubeGpu COMMAND CoinRenderImageNormalTexgenGpuTest --cube-qzero)
		add_test(NAME CoinRenderImageNormalTexgenVolumeGpu COMMAND CoinRenderImageNormalTexgenGpuTest --volume)
		set_tests_properties(CoinRenderImageNormalTexgenVolumeGpu PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		set_tests_properties(CoinRenderImageNormalTexgenCubeGpu PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		set_tests_properties(CoinRenderImageNormalTexgenGpu PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		foreach(route staged direct)
			add_test(NAME CoinRenderImageNormalTexgenRtt_${route} COMMAND CoinRenderImageNormalTexgenGpuTest --rtt-${route})
			set_tests_properties(CoinRenderImageNormalTexgenRtt_${route} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endforeach()
	endif()
	if(COIN_BUILD_LEGACY_GL_RENDERER AND TARGET OpenGL::GL)
		add_executable(CoinRenderImageNormalAntecedentGpuTest CoinRenderImageNormalAntecedentGpuTest.cpp)
		target_sources(CoinRenderImageNormalAntecedentGpuTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		target_compile_definitions(CoinRenderImageNormalAntecedentGpuTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderImageNormalAntecedentGpuTest CoinRender Coin OpenGL::GL)
		target_include_directories(CoinRenderImageNormalAntecedentGpuTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderImageNormalAntecedentNative COMMAND CoinRenderImageNormalAntecedentGpuTest --native)
		add_test(NAME CoinRenderImageNormalAntecedentGpu COMMAND CoinRenderImageNormalAntecedentGpuTest --gpu --portable-sampling)
		set_tests_properties(CoinRenderImageNormalAntecedentNative CoinRenderImageNormalAntecedentGpu
			PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_VERTEX_ARRAYS=1;COIN_VBO_MIN_LIMIT=20")
		set_tests_properties(CoinRenderImageNormalAntecedentGpu PROPERTIES SKIP_RETURN_CODE 77)
		add_executable(CoinRenderImageNormalShapeHistoryTest CoinRenderImageNormalShapeHistoryTest.cpp)
		target_sources(CoinRenderImageNormalShapeHistoryTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		set_target_properties(CoinRenderImageNormalShapeHistoryTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderImageNormalShapeHistoryTest CoinRender Coin OpenGL::GL)
		target_include_directories(CoinRenderImageNormalShapeHistoryTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderImageNormalShapeHistory COMMAND CoinRenderImageNormalShapeHistoryTest)
		set_tests_properties(CoinRenderImageNormalShapeHistory PROPERTIES TIMEOUT 120 RESOURCE_LOCK coinrender_gpu)
	endif()
	add_executable(CoinRenderImageTexgenSubclassTest CoinRenderImageTexgenSubclassTest.cpp)
	target_sources(CoinRenderImageTexgenSubclassTest PRIVATE
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
	set_target_properties(CoinRenderImageTexgenSubclassTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderImageTexgenSubclassTest CoinRender Coin)
	target_include_directories(CoinRenderImageTexgenSubclassTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderImageTexgenSubclass COMMAND CoinRenderImageTexgenSubclassTest)
	set_tests_properties(CoinRenderImageTexgenSubclass PROPERTIES TIMEOUT 60)
	add_executable(CoinRenderImageNormalAntecedentTest CoinRenderImageNormalAntecedentTest.cpp)
	target_sources(CoinRenderImageNormalAntecedentTest PRIVATE
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
	target_compile_definitions(CoinRenderImageNormalAntecedentTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
	target_link_libraries(CoinRenderImageNormalAntecedentTest CoinRender Coin)
	target_include_directories(CoinRenderImageNormalAntecedentTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderImageNormalSortedAnnotation COMMAND CoinRenderImageNormalAntecedentTest --sorted-annotation)
	add_test(NAME CoinRenderImageNormalTextureUnits COMMAND CoinRenderImageNormalAntecedentTest --texture-units)
	set_tests_properties(CoinRenderImageNormalSortedAnnotation CoinRenderImageNormalTextureUnits PROPERTIES TIMEOUT 120)
	add_test(NAME CoinRenderImageNormalVertexProperty COMMAND CoinRenderImageNormalAntecedentTest --vertex-property)
	add_test(NAME CoinRenderImageNormalVertexPropertyPlanOnly COMMAND CoinRenderImageNormalAntecedentTest --plan-only)
	set_tests_properties(CoinRenderImageNormalVertexProperty CoinRenderImageNormalVertexPropertyPlanOnly
		PROPERTIES TIMEOUT 120 ENVIRONMENT "COIN_VERTEX_ARRAYS=1;COIN_VBO_MIN_LIMIT=20")
	add_test(NAME CoinRenderImageNormalDeferredHistory COMMAND CoinRenderImageNormalAntecedentTest --deferred)
	add_test(NAME CoinRenderImageNormalCulledHistory COMMAND CoinRenderImageNormalAntecedentTest --culled)
	set_tests_properties(CoinRenderImageNormalDeferredHistory CoinRenderImageNormalCulledHistory
		PROPERTIES TIMEOUT 120 ENVIRONMENT "COIN_VERTEX_ARRAYS=1;COIN_VBO_MIN_LIMIT=20")
	add_executable(CoinRenderImageNormalTexgenTest CoinRenderImageNormalTexgenTest.cpp)
	target_sources(CoinRenderImageNormalTexgenTest PRIVATE
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
	set_target_properties(CoinRenderImageNormalTexgenTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderImageNormalTexgenTest CoinRender Coin)
	target_include_directories(CoinRenderImageNormalTexgenTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderImageNormalTexgenCapture COMMAND CoinRenderImageNormalTexgenTest --capture)
	set_tests_properties(CoinRenderImageNormalTexgenCapture PROPERTIES TIMEOUT 60)
	add_test(NAME CoinRenderImageNormalTexgenHistory COMMAND CoinRenderImageNormalTexgenTest --history)
	set_tests_properties(CoinRenderImageNormalTexgenHistory PROPERTIES TIMEOUT 60)
	add_test(NAME CoinRenderImageNormalTexgenQHistory COMMAND CoinRenderImageNormalTexgenTest --q-history)
	add_test(NAME CoinRenderImageNormalTexgenAtomic COMMAND CoinRenderImageNormalTexgenTest --atomic)
	add_test(NAME CoinRenderImageNormalTexgenCubeQzero COMMAND CoinRenderImageNormalTexgenTest --cube-qzero)
	add_test(NAME CoinRenderImageNormalTexgenVolume COMMAND CoinRenderImageNormalTexgenTest --volume)
	add_test(NAME CoinRenderImageNormalTexgenIndeterminate COMMAND CoinRenderImageNormalTexgenTest --indeterminate)
	add_test(NAME CoinRenderImageNormalTexgenUnknownGuards COMMAND CoinRenderImageNormalTexgenTest --unknown-guards)
	set_tests_properties(CoinRenderImageNormalTexgenUnknownGuards PROPERTIES TIMEOUT 60 ENVIRONMENT "COIN_VERTEX_ARRAYS=1;COIN_VBO_MIN_LIMIT=20")
	set_tests_properties(CoinRenderImageNormalTexgenIndeterminate PROPERTIES TIMEOUT 60 ENVIRONMENT "COIN_VERTEX_ARRAYS=1;COIN_VBO_MIN_LIMIT=20")
	add_test(NAME CoinRenderImageNormalTexgenTransactionalHistory COMMAND CoinRenderImageNormalTexgenTest --transactional-history)
	add_test(NAME CoinRenderImageNormalTexgenEligibility COMMAND CoinRenderImageNormalTexgenTest --eligibility)
	add_test(NAME CoinRenderImageNormalTexgenSameGraphHistory COMMAND CoinRenderImageNormalTexgenTest --same-graph-history)
	set_tests_properties(CoinRenderImageNormalTexgenQHistory CoinRenderImageNormalTexgenAtomic PROPERTIES TIMEOUT 60)
	add_executable(CoinRenderRasterMsaaTest CoinRenderRasterMsaaTest.cpp)
	target_sources(CoinRenderRasterMsaaTest PRIVATE
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
	set_target_properties(CoinRenderRasterMsaaTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderRasterMsaaTest CoinRender Coin)
	if(X11_FOUND)
		target_compile_definitions(CoinRenderRasterMsaaTest PRIVATE COIN_RASTER_MSAA_X11)
		target_include_directories(CoinRenderRasterMsaaTest PRIVATE ${X11_INCLUDE_DIR})
		target_link_libraries(CoinRenderRasterMsaaTest ${X11_LIBRARIES})
		add_test(NAME CoinRenderRasterMsaa_Window4 COMMAND CoinRenderRasterMsaaTest --window --samples 4)
		set_tests_properties(CoinRenderRasterMsaa_Window4 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endif()
	foreach(focal inherited shadow-inherited annotation object-modes states)
		add_test(NAME CoinRenderRasterMsaa_${focal}4 COMMAND CoinRenderRasterMsaaTest --${focal} --samples 4)
		set_tests_properties(CoinRenderRasterMsaa_${focal}4 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endforeach()
	foreach(rttmode staged direct)
		foreach(alpha_mode object peeling weighted)
			add_test(NAME CoinRenderRasterMsaa_RttAlpha_${rttmode}_${alpha_mode}4 COMMAND CoinRenderRasterMsaaTest --rtt-${rttmode} --rtt-alpha --samples 4 --mechanism ${alpha_mode})
			set_tests_properties(CoinRenderRasterMsaa_RttAlpha_${rttmode}_${alpha_mode}4 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endforeach()
		add_test(NAME CoinRenderRasterMsaa_Rtt_${rttmode} COMMAND CoinRenderRasterMsaaTest --rtt-${rttmode} --budget --invalid --samples 4)
		set_tests_properties(CoinRenderRasterMsaa_Rtt_${rttmode} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endforeach()
	add_test(NAME CoinRenderRasterMsaa_GreyText4 COMMAND CoinRenderRasterMsaaTest --text-only --font "DejaVu Sans" --samples 4)
	set_tests_properties(CoinRenderRasterMsaa_GreyText4 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	target_include_directories(CoinRenderRasterMsaaTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(n 1 2 4)
		foreach(mode object peeling weighted)
			add_test(NAME CoinRenderRasterMsaa_${mode}_${n} COMMAND CoinRenderRasterMsaaTest --smoke --samples ${n} --mechanism ${mode})
			set_tests_properties(CoinRenderRasterMsaa_${mode}_${n} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endforeach()
	endforeach()
	add_executable(CoinRenderImageInheritedIngressTest CoinRenderImageInheritedIngressTest.cpp)
	set_target_properties(CoinRenderImageInheritedIngressTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderImageInheritedIngressTest CoinRender Coin)
	target_include_directories(CoinRenderImageInheritedIngressTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderImageInheritedIngressTest COMMAND CoinRenderImageInheritedIngressTest)
	set_tests_properties(CoinRenderImageInheritedIngressTest PROPERTIES TIMEOUT 60)
	add_executable(CoinRenderImageInheritedShadowTest CoinRenderImageInheritedShadowTest.cpp)
	set_target_properties(CoinRenderImageInheritedShadowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderImageInheritedShadowTest CoinRender Coin)
	target_include_directories(CoinRenderImageInheritedShadowTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderImageInheritedCaptureTest COMMAND CoinRenderScreenContentTest --capture --inherited-only)
	set_tests_properties(CoinRenderImageInheritedCaptureTest PROPERTIES TIMEOUT 60)
	add_test(NAME CoinRenderImageInheritedPortableCaptureTest COMMAND CoinRenderScreenContentTest --capture --inherited-portable-only)
	set_tests_properties(CoinRenderImageInheritedPortableCaptureTest PROPERTIES TIMEOUT 60)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			foreach(renderer ${_coin_bgfx_test_renderers})
				add_test(NAME CoinRenderScreenContent_${renderer} COMMAND CoinRenderScreenContentTest --gpu)
				set_tests_properties(CoinRenderScreenContent_${renderer} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
				add_test(NAME CoinRenderImageInherited_${renderer} COMMAND CoinRenderScreenContentTest --gpu --inherited-only)
				set_tests_properties(CoinRenderImageInherited_${renderer} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
				add_test(NAME CoinRenderImageInheritedPortable_${renderer} COMMAND CoinRenderScreenContentTest --gpu --inherited-portable-only)
				set_tests_properties(CoinRenderImageInheritedPortable_${renderer} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
				foreach(route staged direct)
					add_test(NAME CoinRenderImageInheritedRtt_${renderer}_${route} COMMAND CoinRenderImageInheritedRttTest --${route})
					set_tests_properties(CoinRenderImageInheritedRtt_${renderer}_${route} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
						ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
				endforeach()
				foreach(maps 1 4 8)
					foreach(light spot directional)
						foreach(policy native canonical)
							set(_coin_image_shadow_args)
							if(NOT maps STREQUAL "1")
								list(APPEND _coin_image_shadow_args --maps${maps})
							endif()
							if(light STREQUAL "directional")
								list(APPEND _coin_image_shadow_args --directional)
							endif()
							if(policy STREQUAL "canonical")
								list(APPEND _coin_image_shadow_args --canonical-raster)
							endif()
							add_test(NAME CoinRenderImageInheritedShadow_${renderer}_${maps}_${light}_${policy}
							COMMAND CoinRenderImageInheritedShadowTest ${_coin_image_shadow_args})
							set_tests_properties(CoinRenderImageInheritedShadow_${renderer}_${maps}_${light}_${policy}
								PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
								ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
							add_test(NAME CoinRenderImageInheritedShadowCaster_${renderer}_${maps}_${light}_${policy}
								COMMAND CoinRenderImageInheritedShadowTest ${_coin_image_shadow_args} --caster)
							set_tests_properties(CoinRenderImageInheritedShadowCaster_${renderer}_${maps}_${light}_${policy}
								PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
								ENVIRONMENT "COIN_BGFX_RENDERER=${renderer}")
						endforeach()
					endforeach()
				endforeach()
			endforeach()
		elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
			add_test(NAME CoinRenderScreenContentGpuTest COMMAND CoinRenderScreenContentTest --gpu)
			set_tests_properties(CoinRenderScreenContentGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			add_test(NAME CoinRenderImageInheritedGpuTest COMMAND CoinRenderScreenContentTest --gpu --inherited-only)
			set_tests_properties(CoinRenderImageInheritedGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			add_test(NAME CoinRenderImageInheritedPortableGpuTest COMMAND CoinRenderScreenContentTest --gpu --inherited-portable-only)
			set_tests_properties(CoinRenderImageInheritedPortableGpuTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			foreach(route staged direct)
				add_test(NAME CoinRenderImageInheritedRttGpu_${route} COMMAND CoinRenderImageInheritedRttTest --${route})
				set_tests_properties(CoinRenderImageInheritedRttGpu_${route} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			endforeach()
			foreach(maps 1 4 8)
				foreach(light spot directional)
					foreach(policy native canonical)
						set(_coin_image_shadow_args)
						if(NOT maps STREQUAL "1")
							list(APPEND _coin_image_shadow_args --maps${maps})
						endif()
						if(light STREQUAL "directional")
							list(APPEND _coin_image_shadow_args --directional)
						endif()
						if(policy STREQUAL "canonical")
							list(APPEND _coin_image_shadow_args --canonical-raster)
						endif()
						add_test(NAME CoinRenderImageInheritedShadowGpu_${maps}_${light}_${policy}
							COMMAND CoinRenderImageInheritedShadowTest ${_coin_image_shadow_args})
						set_tests_properties(CoinRenderImageInheritedShadowGpu_${maps}_${light}_${policy}
							PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
						add_test(NAME CoinRenderImageInheritedShadowCasterGpu_${maps}_${light}_${policy}
							COMMAND CoinRenderImageInheritedShadowTest ${_coin_image_shadow_args} --caster)
						set_tests_properties(CoinRenderImageInheritedShadowCasterGpu_${maps}_${light}_${policy}
							PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
					endforeach()
				endforeach()
			endforeach()
		endif()
	endif()
	add_executable(CoinRenderFragmentPolicyTest CoinRenderFragmentPolicyTest.cpp)
	set_target_properties(CoinRenderFragmentPolicyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderFragmentPolicyTest CoinRender Coin)
	if(TARGET OpenGL::GL)
		target_link_libraries(CoinRenderFragmentPolicyTest OpenGL::GL)
	endif()
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
	if(COIN_BUILD_LEGACY_GL_RENDERER AND
	   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
		add_executable(CoinRenderRasterCombinationsTest CoinRenderRasterCombinationsTest.cpp)
		set_target_properties(CoinRenderRasterCombinationsTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderRasterCombinationsTest CoinRender Coin)
		target_include_directories(CoinRenderRasterCombinationsTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		if(UNIX AND NOT APPLE AND NOT ANDROID AND X11_FOUND)
			target_compile_definitions(CoinRenderRasterCombinationsTest PRIVATE COIN_RASTER_X11=1)
			target_link_libraries(CoinRenderRasterCombinationsTest ${X11_LIBRARIES})
			target_include_directories(CoinRenderRasterCombinationsTest PRIVATE ${X11_INCLUDE_DIR})
			add_test(NAME CoinRenderRasterCombinationsWindowTest
				COMMAND CoinRenderRasterCombinationsTest --window)
			add_test(NAME CoinRenderRasterCombinationsCanonicalWindowTest
				COMMAND CoinRenderRasterCombinationsTest --window --canonical-raster)
			add_test(NAME CoinRenderRasterShadowWindowCoreTest
				COMMAND CoinRenderRasterCombinationsTest --window-shadow-core)
			add_test(NAME CoinRenderRasterShadowWindowCoreCanonicalTest
				COMMAND CoinRenderRasterCombinationsTest --window-shadow-core --canonical-raster)
			set_tests_properties(CoinRenderRasterCombinationsWindowTest
				CoinRenderRasterCombinationsCanonicalWindowTest
				CoinRenderRasterShadowWindowCoreTest
				CoinRenderRasterShadowWindowCoreCanonicalTest
				PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endif()
		add_test(NAME CoinRenderRasterCombinationsTest COMMAND CoinRenderRasterCombinationsTest)
		add_test(NAME CoinRenderRasterCombinationsCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --canonical-raster)
		add_test(NAME CoinRenderRasterSortingOnlyTest
			COMMAND CoinRenderRasterCombinationsTest --sorting-only)
		add_test(NAME CoinRenderRasterSortingOnlyCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --sorting-only --canonical-raster)
		add_test(NAME CoinRenderRasterNoMapsTest
			COMMAND CoinRenderRasterCombinationsTest --no-maps-only)
		add_test(NAME CoinRenderRasterNoMapsCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --no-maps-only --canonical-raster)
		add_test(NAME CoinRenderRasterNoMapsCoreTest
			COMMAND CoinRenderRasterCombinationsTest --no-maps-core-only)
		add_test(NAME CoinRenderRasterNoMapsCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --no-maps-core-only --canonical-raster)
		add_test(NAME CoinRenderRasterShadowCoreTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-core-only)
		add_test(NAME CoinRenderRasterShadowCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-core-only --canonical-raster)
		add_test(NAME CoinRenderRasterShadowReceiverCoreTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-receiver-core-only)
		add_test(NAME CoinRenderRasterShadowReceiverCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-receiver-core-only --canonical-raster)
		add_test(NAME CoinRenderRasterShadowReceiverFogCoreTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-receiver-fog-core-only)
		add_test(NAME CoinRenderRasterShadowReceiverFogCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-receiver-fog-core-only --canonical-raster)
		add_test(NAME CoinRenderRasterShadowCombinationsCoreTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-combinations-core-only)
		add_test(NAME CoinRenderRasterShadowCombinationsCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-combinations-core-only --canonical-raster)
		add_test(NAME CoinRenderRasterShadowDepthCoreTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-depth-core-only)
		add_test(NAME CoinRenderRasterShadowDepthCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-depth-core-only --canonical-raster)
		add_test(NAME CoinRenderRasterShadowFogModesCoreTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-fog-modes-core-only)
		add_test(NAME CoinRenderRasterShadowFogModesCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-fog-modes-core-only --canonical-raster)
		add_test(NAME CoinRenderRasterShadowMarkerAnchorFogCoreTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-marker-anchor-fog-core-only)
		add_test(NAME CoinRenderRasterShadowMarkerAnchorFogCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --shadow-marker-anchor-fog-core-only --canonical-raster)
		add_test(NAME CoinRenderRasterCombinationsRttStagedTest
			COMMAND CoinRenderRasterCombinationsTest --rtt-staged)
		add_test(NAME CoinRenderRasterCombinationsRttDirectTest
			COMMAND CoinRenderRasterCombinationsTest --rtt-direct)
		add_test(NAME CoinRenderRasterCombinationsCanonicalRttStagedTest
			COMMAND CoinRenderRasterCombinationsTest --rtt-staged --canonical-raster)
		add_test(NAME CoinRenderRasterCombinationsCanonicalRttDirectTest
			COMMAND CoinRenderRasterCombinationsTest --rtt-direct --canonical-raster)
		add_test(NAME CoinRenderRasterShadowRttStagedCoreTest
			COMMAND CoinRenderRasterCombinationsTest --rtt-shadow-staged)
		add_test(NAME CoinRenderRasterShadowRttDirectCoreTest
			COMMAND CoinRenderRasterCombinationsTest --rtt-shadow-direct)
		add_test(NAME CoinRenderRasterShadowRttStagedCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --rtt-shadow-staged --canonical-raster)
		add_test(NAME CoinRenderRasterShadowRttDirectCoreCanonicalTest
			COMMAND CoinRenderRasterCombinationsTest --rtt-shadow-direct --canonical-raster)
		set_tests_properties(CoinRenderRasterCombinationsTest CoinRenderRasterCombinationsCanonicalTest
			CoinRenderRasterSortingOnlyTest CoinRenderRasterSortingOnlyCanonicalTest
			PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		set_tests_properties(CoinRenderRasterNoMapsTest CoinRenderRasterNoMapsCanonicalTest
			CoinRenderRasterNoMapsCoreTest CoinRenderRasterNoMapsCoreCanonicalTest
			CoinRenderRasterShadowCoreTest CoinRenderRasterShadowCoreCanonicalTest
			CoinRenderRasterShadowReceiverCoreTest
			CoinRenderRasterShadowReceiverCoreCanonicalTest
			CoinRenderRasterShadowReceiverFogCoreTest
			CoinRenderRasterShadowReceiverFogCoreCanonicalTest
			CoinRenderRasterShadowCombinationsCoreTest
			CoinRenderRasterShadowCombinationsCoreCanonicalTest
			CoinRenderRasterShadowDepthCoreTest
			CoinRenderRasterShadowDepthCoreCanonicalTest
			CoinRenderRasterShadowFogModesCoreTest
			CoinRenderRasterShadowFogModesCoreCanonicalTest
			CoinRenderRasterShadowMarkerAnchorFogCoreTest
			CoinRenderRasterShadowMarkerAnchorFogCoreCanonicalTest
			PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		set_tests_properties(CoinRenderRasterCombinationsRttStagedTest
			CoinRenderRasterCombinationsRttDirectTest
			CoinRenderRasterCombinationsCanonicalRttStagedTest
			CoinRenderRasterCombinationsCanonicalRttDirectTest
			CoinRenderRasterShadowRttStagedCoreTest
			CoinRenderRasterShadowRttDirectCoreTest
			CoinRenderRasterShadowRttStagedCoreCanonicalTest
			CoinRenderRasterShadowRttDirectCoreCanonicalTest
			PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endif()
	add_executable(CoinRenderShadowEightMapTest CoinRenderShadowEightMapTest.cpp)
	set_target_properties(CoinRenderShadowEightMapTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderShadowEightMapTest CoinRender Coin)
	target_include_directories(CoinRenderShadowEightMapTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderShadowEightMapTest COMMAND CoinRenderShadowEightMapTest)
	set_tests_properties(CoinRenderShadowEightMapTest PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	add_executable(CoinRenderRasterAlphaBoundaryTest CoinRenderRasterAlphaBoundaryTest.cpp)
	set_target_properties(CoinRenderRasterAlphaBoundaryTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderRasterAlphaBoundaryTest CoinRender Coin)
	target_include_directories(CoinRenderRasterAlphaBoundaryTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderRasterAlphaBoundaryCpuTest COMMAND CoinRenderRasterAlphaBoundaryTest --cpu)
	set_tests_properties(CoinRenderRasterAlphaBoundaryCpuTest PROPERTIES TIMEOUT 120)
	add_test(NAME CoinRenderRasterAlphaBoundaryGpuTest COMMAND CoinRenderRasterAlphaBoundaryTest --gpu)
	add_test(NAME CoinRenderRasterAlphaCasterBoundaryGpuTest COMMAND CoinRenderRasterAlphaBoundaryTest --gpu --casters)
	set_tests_properties(CoinRenderRasterAlphaBoundaryGpuTest CoinRenderRasterAlphaCasterBoundaryGpuTest
		PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	add_test(NAME CoinRenderRasterNonproducerPathsCpuTest COMMAND CoinRenderRasterAlphaBoundaryTest --cpu --paths)
	set_tests_properties(CoinRenderRasterNonproducerPathsCpuTest PROPERTIES TIMEOUT 120)
	add_test(NAME CoinRenderRasterNonproducerPathsGpuTest COMMAND CoinRenderRasterAlphaBoundaryTest --gpu --paths)
	add_test(NAME CoinRenderRasterSpotNumericalGpuTest COMMAND CoinRenderRasterAlphaBoundaryTest --gpu --spot)
	set_tests_properties(CoinRenderRasterNonproducerPathsGpuTest CoinRenderRasterSpotNumericalGpuTest
		PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	add_executable(CoinRenderShadowAlphaMapTest CoinRenderShadowAlphaTest.cpp)
	set_target_properties(CoinRenderShadowAlphaMapTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderShadowAlphaMapTest CoinRender Coin)
	target_include_directories(CoinRenderShadowAlphaMapTest PRIVATE
		${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	if(UNIX AND NOT APPLE AND NOT ANDROID AND X11_FOUND)
		target_compile_definitions(CoinRenderShadowAlphaMapTest PRIVATE COIN_ALPHA_X11=1)
		target_link_libraries(CoinRenderShadowAlphaMapTest ${X11_LIBRARIES})
		target_include_directories(CoinRenderShadowAlphaMapTest PRIVATE ${X11_INCLUDE_DIR})
		add_test(NAME CoinRenderShadowAlphaWindowNativeTest
			COMMAND CoinRenderShadowAlphaMapTest --window)
		add_test(NAME CoinRenderShadowAlphaWindowCanonicalTest
			COMMAND CoinRenderShadowAlphaMapTest --window --canonical-raster)
		set_tests_properties(CoinRenderShadowAlphaWindowNativeTest
			CoinRenderShadowAlphaWindowCanonicalTest PROPERTIES TIMEOUT 300
			SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endif()
	add_test(NAME CoinRenderShadowAlphaMapNativeTest COMMAND CoinRenderShadowAlphaMapTest)
	add_test(NAME CoinRenderShadowAlphaMapCanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaMap4NativeTest
		COMMAND CoinRenderShadowAlphaMapTest --maps4)
	add_test(NAME CoinRenderShadowAlphaMap4CanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --maps4 --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaMap8NativeTest
		COMMAND CoinRenderShadowAlphaMapTest --maps8)
	add_test(NAME CoinRenderShadowAlphaMap8CanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --maps8 --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaRasterNativeTest
		COMMAND CoinRenderShadowAlphaMapTest --raster)
	add_test(NAME CoinRenderShadowAlphaRasterCanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --raster --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaReceiverNativeTest
		COMMAND CoinRenderShadowAlphaMapTest --receiver)
	add_test(NAME CoinRenderShadowAlphaReceiverCanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --receiver --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaNoMapsNativeTest
		COMMAND CoinRenderShadowAlphaMapTest --no-maps)
	add_test(NAME CoinRenderShadowAlphaNoMapsCanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --no-maps --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaSeparatorNativeTest
		COMMAND CoinRenderShadowAlphaMapTest --separator)
	add_test(NAME CoinRenderShadowAlphaSeparatorCanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --separator --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaMixedMapNativeTest
		COMMAND CoinRenderShadowAlphaMapTest --mixed-map-scene)
	add_test(NAME CoinRenderShadowAlphaMixedMapCanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --mixed-map-scene --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaRasterMaps4NativeTest
		COMMAND CoinRenderShadowAlphaMapTest --raster-maps4)
	add_test(NAME CoinRenderShadowAlphaRasterMaps4CanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --raster-maps4 --canonical-raster)
	add_test(NAME CoinRenderShadowAlphaRasterMaps8NativeTest
		COMMAND CoinRenderShadowAlphaMapTest --raster-maps8)
	add_test(NAME CoinRenderShadowAlphaRasterMaps8CanonicalTest
		COMMAND CoinRenderShadowAlphaMapTest --raster-maps8 --canonical-raster)
	foreach(_alpha_rtt_route staged direct)
		add_test(NAME CoinRenderShadowAlphaRTT${_alpha_rtt_route}NativeTest
			COMMAND CoinRenderShadowAlphaMapTest --rtt-${_alpha_rtt_route})
		add_test(NAME CoinRenderShadowAlphaRTT${_alpha_rtt_route}CanonicalTest
			COMMAND CoinRenderShadowAlphaMapTest --rtt-${_alpha_rtt_route} --canonical-raster)
		set_tests_properties(CoinRenderShadowAlphaRTT${_alpha_rtt_route}NativeTest
			CoinRenderShadowAlphaRTT${_alpha_rtt_route}CanonicalTest PROPERTIES
			TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	endforeach()
	set_tests_properties(CoinRenderShadowAlphaMapNativeTest
		CoinRenderShadowAlphaMapCanonicalTest
		CoinRenderShadowAlphaMap4NativeTest
		CoinRenderShadowAlphaMap4CanonicalTest
		CoinRenderShadowAlphaMap8NativeTest
		CoinRenderShadowAlphaMap8CanonicalTest
		CoinRenderShadowAlphaRasterNativeTest
		CoinRenderShadowAlphaRasterCanonicalTest
		CoinRenderShadowAlphaReceiverNativeTest
		CoinRenderShadowAlphaReceiverCanonicalTest
		CoinRenderShadowAlphaNoMapsNativeTest
		CoinRenderShadowAlphaNoMapsCanonicalTest
		CoinRenderShadowAlphaSeparatorNativeTest
		CoinRenderShadowAlphaSeparatorCanonicalTest
		CoinRenderShadowAlphaMixedMapNativeTest
		CoinRenderShadowAlphaMixedMapCanonicalTest
		CoinRenderShadowAlphaRasterMaps4NativeTest
		CoinRenderShadowAlphaRasterMaps4CanonicalTest
		CoinRenderShadowAlphaRasterMaps8NativeTest
		CoinRenderShadowAlphaRasterMaps8CanonicalTest PROPERTIES TIMEOUT 300
		SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		add_executable(CoinRenderShadowReferenceTest CoinRenderShadowReferenceTest.cpp)
		add_executable(CoinRenderAuthoredCompositionTest CoinRenderAuthoredCompositionTest.cpp)
		target_sources(CoinRenderAuthoredCompositionTest PRIVATE
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
		target_compile_definitions(CoinRenderAuthoredCompositionTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
		target_link_libraries(CoinRenderAuthoredCompositionTest CoinRender Coin)
		if(TARGET OpenGL::GL)
			target_link_libraries(CoinRenderAuthoredCompositionTest OpenGL::GL)
		else()
			target_link_libraries(CoinRenderAuthoredCompositionTest ${OPENGL_gl_LIBRARY})
		endif()
		target_include_directories(CoinRenderAuthoredCompositionTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		foreach(authored_rtt_profile R1 R2 R3)
			foreach(authored_rtt_route direct staged)
				add_test(NAME CoinRenderAuthoredComposition${authored_rtt_profile}_${authored_rtt_route}Cpu COMMAND CoinRenderAuthoredCompositionTest --cpu --profile ${authored_rtt_profile} --route ${authored_rtt_route})
				set_tests_properties(CoinRenderAuthoredComposition${authored_rtt_profile}_${authored_rtt_route}Cpu PROPERTIES TIMEOUT 120)
				add_test(NAME CoinRenderAuthoredComposition${authored_rtt_profile}_${authored_rtt_route}Gpu COMMAND CoinRenderAuthoredCompositionTest --gpu --profile ${authored_rtt_profile} --route ${authored_rtt_route})
				set_tests_properties(CoinRenderAuthoredComposition${authored_rtt_profile}_${authored_rtt_route}Gpu PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
			endforeach()
		endforeach()
		add_test(NAME CoinRenderAuthoredCompositionT1Cpu COMMAND CoinRenderAuthoredCompositionTest --cpu --profile T1)
		set_tests_properties(CoinRenderAuthoredCompositionT1Cpu PROPERTIES TIMEOUT 120)
		add_test(NAME CoinRenderAuthoredCompositionT1Gpu COMMAND CoinRenderAuthoredCompositionTest --gpu --profile T1)
		set_tests_properties(CoinRenderAuthoredCompositionT1Gpu PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderAuthoredCompositionT2Cpu COMMAND CoinRenderAuthoredCompositionTest --cpu --profile T2)
		set_tests_properties(CoinRenderAuthoredCompositionT2Cpu PROPERTIES TIMEOUT 120)
		add_test(NAME CoinRenderAuthoredCompositionT2Gpu COMMAND CoinRenderAuthoredCompositionTest --gpu --profile T2)
		set_tests_properties(CoinRenderAuthoredCompositionT2Gpu PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderAuthoredCompositionT3Cpu COMMAND CoinRenderAuthoredCompositionTest --cpu --profile T3)
		set_tests_properties(CoinRenderAuthoredCompositionT3Cpu PROPERTIES TIMEOUT 120)
		add_test(NAME CoinRenderAuthoredCompositionT3Gpu COMMAND CoinRenderAuthoredCompositionTest --gpu --profile T3)
		set_tests_properties(CoinRenderAuthoredCompositionT3Gpu PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		foreach(authored_oit_profile T4 T5)
			add_test(NAME CoinRenderAuthoredComposition${authored_oit_profile}Cpu COMMAND CoinRenderAuthoredCompositionTest --cpu --profile ${authored_oit_profile})
			set_tests_properties(CoinRenderAuthoredComposition${authored_oit_profile}Cpu PROPERTIES TIMEOUT 120)
			add_test(NAME CoinRenderAuthoredComposition${authored_oit_profile}Gpu COMMAND CoinRenderAuthoredCompositionTest --gpu --profile ${authored_oit_profile})
			set_tests_properties(CoinRenderAuthoredComposition${authored_oit_profile}Gpu PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		endforeach()
		add_executable(CoinRenderShadowMapCameraTest CoinRenderShadowMapCameraTest.cpp)
		set_target_properties(CoinRenderShadowMapCameraTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderShadowMapCameraTest CoinRender Coin)
		if(TARGET OpenGL::GL)
			target_link_libraries(CoinRenderShadowMapCameraTest OpenGL::GL)
		else()
			target_link_libraries(CoinRenderShadowMapCameraTest ${OPENGL_gl_LIBRARY})
		endif()
		target_include_directories(CoinRenderShadowMapCameraTest PRIVATE
			${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderShadowMapCameraTest COMMAND CoinRenderShadowMapCameraTest)
		add_test(NAME CoinRenderShadowMapCameraCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraNativeOracleTest
			COMMAND CoinRenderShadowMapCameraTest --geometry --native-oracle-only)
		add_test(NAME CoinRenderShadowMapCameraGeometryTest
			COMMAND CoinRenderShadowMapCameraTest --geometry)
		add_test(NAME CoinRenderShadowMapCameraSlotsTest
			COMMAND CoinRenderShadowMapCameraTest --slots)
		add_test(NAME CoinRenderShadowMapCameraRasterNativeTest
			COMMAND CoinRenderShadowMapCameraTest --raster-native)
		add_test(NAME CoinRenderShadowMapCameraGeometryCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --geometry --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraSlotsCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --slots --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraRasterNativeCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --raster-native --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraRectangularNativeOracleTest
			COMMAND CoinRenderShadowMapCameraTest --rectangular --native-oracle-only)
		add_test(NAME CoinRenderShadowMapCameraRectangularTest
			COMMAND CoinRenderShadowMapCameraTest --rectangular)
		add_test(NAME CoinRenderShadowMapCameraRectangularCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --rectangular --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraRectangularCoreCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --rectangular-core --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraRasterCoreTest
			COMMAND CoinRenderShadowMapCameraTest --raster-core)
		add_test(NAME CoinRenderShadowMapCameraRasterCoreCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --raster-core --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraNearPlanesTest
			COMMAND CoinRenderShadowMapCameraTest --near-planes)
		add_test(NAME CoinRenderShadowMapCameraNearPlanesCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --near-planes --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraModelResetTest
			COMMAND CoinRenderShadowMapCameraTest --model-reset)
		add_test(NAME CoinRenderShadowMapCameraModelResetCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --model-reset --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraModelResetCoreTest
			COMMAND CoinRenderShadowMapCameraTest --model-reset-core)
		add_test(NAME CoinRenderShadowMapCameraModelResetCoreCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --model-reset-core --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraSharedNodeTest
			COMMAND CoinRenderShadowMapCameraTest --shared-camera)
		add_test(NAME CoinRenderShadowMapCameraSharedNodeCanonicalTest
			COMMAND CoinRenderShadowMapCameraTest --shared-camera --canonical-raster)
		add_test(NAME CoinRenderShadowMapCameraRectangularVsmCoreTest
			COMMAND CoinRenderShadowMapCameraTest --rectangular-vsm-core --canonical-raster)
		set_tests_properties(CoinRenderShadowMapCameraTest
			CoinRenderShadowMapCameraCanonicalTest
			CoinRenderShadowMapCameraNativeOracleTest
			CoinRenderShadowMapCameraGeometryTest
			CoinRenderShadowMapCameraSlotsTest
			CoinRenderShadowMapCameraRasterNativeTest
			CoinRenderShadowMapCameraGeometryCanonicalTest
			CoinRenderShadowMapCameraSlotsCanonicalTest
			CoinRenderShadowMapCameraRasterNativeCanonicalTest
			CoinRenderShadowMapCameraRectangularNativeOracleTest
			CoinRenderShadowMapCameraRectangularTest
			CoinRenderShadowMapCameraRectangularCanonicalTest
			CoinRenderShadowMapCameraRectangularCoreCanonicalTest
			CoinRenderShadowMapCameraRasterCoreTest
			CoinRenderShadowMapCameraRasterCoreCanonicalTest
			CoinRenderShadowMapCameraNearPlanesTest
			CoinRenderShadowMapCameraNearPlanesCanonicalTest
			CoinRenderShadowMapCameraModelResetTest
			CoinRenderShadowMapCameraModelResetCanonicalTest
			CoinRenderShadowMapCameraModelResetCoreTest
			CoinRenderShadowMapCameraModelResetCoreCanonicalTest
			CoinRenderShadowMapCameraSharedNodeTest
			CoinRenderShadowMapCameraSharedNodeCanonicalTest
			CoinRenderShadowMapCameraRectangularVsmCoreTest PROPERTIES
			TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
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
		set_tests_properties(CoinRenderPreciseQuadMeshShadowTest PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		add_test(NAME CoinRenderFlatQuadMeshShadowTest COMMAND CoinRenderShadowReferenceTest --precise-quadmesh-shadow)
		set_tests_properties(CoinRenderFlatQuadMeshShadowTest PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
		if(COIN_RENDER_BACKEND STREQUAL "BGFX")
			set_tests_properties(CoinRenderPreciseQuadMeshShadowTest PROPERTIES
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1;COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
			set_tests_properties(CoinRenderFlatQuadMeshShadowTest PROPERTIES
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=0;COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
			set_tests_properties(CoinRenderPreciseQuadMeshShadowTest PROPERTIES
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1;COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
			set_tests_properties(CoinRenderFlatQuadMeshShadowTest PROPERTIES
				ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=0;COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1;COIN_GLXGLUE_NO_PBUFFERS=1;COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
		endif()
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
			add_test(NAME CoinRenderSurfaceAcquireTimeoutStrictTest COMMAND CoinRenderSurfaceTest --acquire-timeout-only --require-vulkan)
			set_tests_properties(CoinRenderSurfaceAcquireTimeoutStrictTest PROPERTIES TIMEOUT 30 RESOURCE_LOCK coinrender_gpu)
			add_test(NAME CoinRenderSurfaceLegacyTimeoutRecoveryStrictTest COMMAND CoinRenderSurfaceTest --legacy-timeout-recovery-only --require-vulkan)
			set_tests_properties(CoinRenderSurfaceLegacyTimeoutRecoveryStrictTest PROPERTIES TIMEOUT 30 RESOURCE_LOCK coinrender_gpu)
		endif()
	endif()
endif()

if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
  add_executable(CoinRenderBumpProfileTest CoinRenderBumpProfileTest.cpp)
  target_sources(CoinRenderBumpProfileTest PRIVATE ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderBumpProfileTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderBumpProfileTest CoinRender Coin)
  target_include_directories(CoinRenderBumpProfileTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(bump_samples 1 2 4)
    add_test(NAME CoinRenderBumpProfileMsaa${bump_samples} COMMAND CoinRenderBumpProfileTest --msaa --samples ${bump_samples})
    set_tests_properties(CoinRenderBumpProfileMsaa${bump_samples} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  add_test(NAME CoinRenderBumpProfileSmoke COMMAND CoinRenderBumpProfileTest --smoke)
  add_test(NAME CoinRenderBumpProfileSrgb COMMAND CoinRenderBumpProfileTest --smoke --srgb)
  set_tests_properties(CoinRenderBumpProfileSmoke CoinRenderBumpProfileSrgb PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderBumpProfileTest)
  foreach(bump_focal delivery coords lighting cache height sampling anisotropy async-refusal slots)
    add_test(NAME CoinRenderBumpProfile_${bump_focal} COMMAND CoinRenderBumpProfileTest --${bump_focal})
    if(bump_focal STREQUAL "delivery")
      set_tests_properties(CoinRenderBumpProfile_${bump_focal} PROPERTIES TIMEOUT 180)
    else()
      set_tests_properties(CoinRenderBumpProfile_${bump_focal} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endif()
  endforeach()
  foreach(bump_route direct staged)
    add_test(NAME CoinRenderBumpProfile_rtt_${bump_route} COMMAND CoinRenderBumpProfileTest --rtt --${bump_route} --samples 4)
    set_tests_properties(CoinRenderBumpProfile_rtt_${bump_route} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderBumpProfileTest AND X11_FOUND)
  add_executable(CoinRenderBumpProfileWindowTest CoinRenderBumpProfileWindowTest.cpp)
  target_compile_definitions(CoinRenderBumpProfileWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderBumpProfileWindowTest CoinRender Coin ${X11_LIBRARIES})
  target_include_directories(CoinRenderBumpProfileWindowTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
  foreach(bump_samples 1 2 4)
    add_test(NAME CoinRenderBumpProfileWindow${bump_samples} COMMAND CoinRenderBumpProfileWindowTest --samples ${bump_samples})
    set_tests_properties(CoinRenderBumpProfileWindow${bump_samples} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderBumpProfileWindowTest)
  foreach(bump_route direct staged)
    add_test(NAME CoinRenderBumpProfileWindowRtt_${bump_route} COMMAND CoinRenderBumpProfileWindowTest --rtt --${bump_route} --samples 4)
    set_tests_properties(CoinRenderBumpProfileWindowRtt_${bump_route} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderBumpProfileTest AND COIN_RENDER_BACKEND STREQUAL "BGFX" AND UNIX)
  target_compile_definitions(CoinRenderBumpProfileTest PRIVATE COIN_BUMP_TEST_BGFX)
  add_test(NAME CoinRenderBumpProfileCacheTrace COMMAND CoinRenderBumpProfileTest --cache-trace)
  set_tests_properties(CoinRenderBumpProfileCacheTrace PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderBumpProfileTest)
  foreach(bump_samples 2 4)
    add_test(NAME CoinRenderBumpProfileSlotsMsaa${bump_samples} COMMAND CoinRenderBumpProfileTest --slots --samples ${bump_samples})
    set_tests_properties(CoinRenderBumpProfileSlotsMsaa${bump_samples} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderBumpProfileTest AND COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
  target_compile_definitions(CoinRenderBumpProfileTest PRIVATE COIN_BUMP_TEST_RUST_BRIDGE)
endif()

if(TARGET CoinRender)
  add_executable(CoinRenderCubeRttSmokeTest CoinRenderCubeRttSmokeTest.cpp)
  target_link_libraries(CoinRenderCubeRttSmokeTest CoinRender Coin)
  target_include_directories(CoinRenderCubeRttSmokeTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(cube_rtt_route direct staged)
    add_test(NAME CoinRenderCubeRttPublicSmoke_${cube_rtt_route} COMMAND CoinRenderCubeRttSmokeTest --${cube_rtt_route})
    set_tests_properties(CoinRenderCubeRttPublicSmoke_${cube_rtt_route} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRender)
  add_executable(CoinRenderCubeRttTest CoinRenderCubeRttTest.cpp)
  target_sources(CoinRenderCubeRttTest PRIVATE ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderCubeRttTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderCubeRttTest CoinRender Coin)
  target_include_directories(CoinRenderCubeRttTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderCubeRttCpuTest COMMAND CoinRenderCubeRttTest --cpu)
  set_tests_properties(CoinRenderCubeRttCpuTest PROPERTIES TIMEOUT 180)
  foreach(cube_rtt_focal faces camera orthographic transformed models invalid sampling state alpha transport override nested)
    add_test(NAME CoinRenderCubeRtt_${cube_rtt_focal} COMMAND CoinRenderCubeRttTest --${cube_rtt_focal})
    set_tests_properties(CoinRenderCubeRtt_${cube_rtt_focal} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderCubeRttTest AND X11_FOUND)
  add_executable(CoinRenderCubeRttWindowTest CoinRenderCubeRttWindowTest.cpp)
  target_compile_definitions(CoinRenderCubeRttWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderCubeRttWindowTest CoinRender Coin ${X11_LIBRARIES})
  target_include_directories(CoinRenderCubeRttWindowTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
  foreach(cube_rtt_route direct staged)
    add_test(NAME CoinRenderCubeRttWindow_${cube_rtt_route} COMMAND CoinRenderCubeRttWindowTest --${cube_rtt_route})
    set_tests_properties(CoinRenderCubeRttWindow_${cube_rtt_route} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderCubeRttTest)
  foreach(cube_samples 1 2 4)
    add_test(NAME CoinRenderCubeRttMsaa${cube_samples} COMMAND CoinRenderCubeRttTest --msaa --samples ${cube_samples})
    set_tests_properties(CoinRenderCubeRttMsaa${cube_samples} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderCubeRttTest)
  add_test(NAME CoinRenderCubeRttGraphDepth8 COMMAND CoinRenderCubeRttTest --graph-depth --levels 8)
  set_tests_properties(CoinRenderCubeRttGraphDepth8 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderCubeRttTest)
  if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    target_compile_definitions(CoinRenderCubeRttTest PRIVATE COIN_CUBE_RTT_TEST_RUST)
  elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
    target_compile_definitions(CoinRenderCubeRttTest PRIVATE COIN_CUBE_RTT_TEST_BGFX)
  endif()
  set_tests_properties(CoinRenderCubeRttCpuTest PROPERTIES RESOURCE_LOCK coinrender_gpu)
  add_test(NAME CoinRenderCubeRttCore COMMAND CoinRenderCubeRttTest --core)
  foreach(cube_route direct staged)
    add_test(NAME CoinRenderCubeRttLate_${cube_route} COMMAND CoinRenderCubeRttTest --late --${cube_route})
    set_tests_properties(CoinRenderCubeRttLate_${cube_route} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderCubeRttTest)
  foreach(cube_focal mixed tickets)
    add_test(NAME CoinRenderCubeRtt_${cube_focal} COMMAND CoinRenderCubeRttTest --${cube_focal})
    set_tests_properties(CoinRenderCubeRtt_${cube_focal} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderCubeRttTest)
  add_test(NAME CoinRenderCubeRttBudget COMMAND CoinRenderCubeRttTest --budget)
  add_test(NAME CoinRenderCubeRttDirectProof COMMAND CoinRenderCubeRttTest --direct-proof)
  set_tests_properties(CoinRenderCubeRttDirectProof PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderCubeRttTest)
  add_test(NAME CoinRenderCubeRttProducerState COMMAND CoinRenderCubeRttTest --producer-state)
  set_tests_properties(CoinRenderCubeRttProducerState PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderCubeRttTest)
  foreach(cube_method 1 2 3)
    add_test(NAME CoinRenderCubeRttProducerLayers${cube_method} COMMAND CoinRenderCubeRttTest --layers --method ${cube_method})
    set_tests_properties(CoinRenderCubeRttProducerLayers${cube_method} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderCubeRttTest)
  add_test(NAME CoinRenderCubeRttProducerShadow COMMAND CoinRenderCubeRttTest --producer-shadow)
  set_tests_properties(CoinRenderCubeRttProducerShadow PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderCubeRttTest)
  foreach(cube_extent 1 2 2048)
    foreach(cube_mips base mips)
      set(cube_mip_flag)
      if(cube_mips STREQUAL "mips")
        set(cube_mip_flag --mips)
      endif()
      foreach(cube_route direct staged)
        add_test(NAME CoinRenderCubeRttLimit_${cube_extent}_${cube_mips}_${cube_route} COMMAND CoinRenderCubeRttTest --limits --extent ${cube_extent} ${cube_mip_flag} --${cube_route})
        set_tests_properties(CoinRenderCubeRttLimit_${cube_extent}_${cube_mips}_${cube_route} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      endforeach()
    endforeach()
  endforeach()
endif()

if(TARGET CoinRenderCubeRttTest AND COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
  add_test(NAME CoinRenderCubeRttRaw61 COMMAND CoinRenderCubeRttTest --raw61)
  set_tests_properties(CoinRenderCubeRttRaw61 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderCubeRttTest)
  add_test(NAME CoinRenderCubeRttCallbackDepth3 COMMAND CoinRenderCubeRttTest --callback-depth --levels 3)
  set_tests_properties(CoinRenderCubeRttCallbackDepth3 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderCubeRttTest)
  add_executable(CoinRenderCubeRttSharingTest CoinRenderCubeRttSharingTest.cpp)
  target_compile_definitions(CoinRenderCubeRttSharingTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderCubeRttSharingTest CoinRender Coin)
  target_include_directories(CoinRenderCubeRttSharingTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(cube_sharing shared dependency-filter observable transitive occurrence-budget contexts function stereo stereo-shared)
    add_test(NAME CoinRenderCubeRttSharing_${cube_sharing} COMMAND CoinRenderCubeRttSharingTest --${cube_sharing})
    set_tests_properties(CoinRenderCubeRttSharing_${cube_sharing} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  add_test(NAME CoinRenderCubeRttOccurrenceCore COMMAND CoinRenderCubeRttSharingTest --occurrence-core)
endif()

if(TARGET CoinRenderCubeRttTest)
  add_executable(CoinRenderCubeRttCapacityTest CoinRenderCubeRttCapacityTest.cpp)
  target_sources(CoinRenderCubeRttCapacityTest PRIVATE ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderCubeRttCapacityTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderCubeRttCapacityTest CoinRender Coin)
  target_include_directories(CoinRenderCubeRttCapacityTest PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderCubeRttCapacityCore COMMAND CoinRenderCubeRttCapacityTest --core)
  foreach(cube_capacity plain mixed)
    set(cube_capacity_args)
    if(cube_capacity STREQUAL "mixed")
      set(cube_capacity_args --mixed)
    endif()
    add_test(NAME CoinRenderCubeRttCapacity_${cube_capacity} COMMAND CoinRenderCubeRttCapacityTest ${cube_capacity_args})
    set_tests_properties(CoinRenderCubeRttCapacity_${cube_capacity} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    target_compile_definitions(CoinRenderCubeRttCapacityTest PRIVATE COIN_CUBE_RTT_TEST_RUST)
    add_test(NAME CoinRenderCubeRttCapacityExternal COMMAND CoinRenderCubeRttCapacityTest --external)
    set_tests_properties(CoinRenderCubeRttCapacityExternal PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

if(TARGET CoinRenderShadowMultitextureWindowTest)
  add_executable(CoinRenderWindowTransitionTest CoinRenderWindowTransitionTest.cpp)
  target_sources(CoinRenderWindowTransitionTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderWindowTransitionTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderWindowTransitionTest CoinRender Coin ${X11_LIBRARIES})
  target_include_directories(CoinRenderWindowTransitionTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
    ${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
  add_test(NAME CoinRenderWindowTransitions COMMAND CoinRenderWindowTransitionTest --samples 4 --portable-sampling)
  set_tests_properties(CoinRenderWindowTransitions PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu ENVIRONMENT "COIN_QUADMESH_PRECISE_LIGHTING=1")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderCubeRttCpuFactoryTests.cmake)

# C17 directional opaque bump + VSM: independent authored numerical witnesses.
if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
  add_executable(CoinRenderBumpShadowTest CoinRenderBumpShadowTest.cpp)
  target_sources(CoinRenderBumpShadowTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderBumpShadowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderBumpShadowTest CoinRender Coin)
  target_include_directories(CoinRenderBumpShadowTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(bump_shadow_mode smoke lighting projection invalid delivery mixed mixed-lighting fractional coverage sampling combine models height coords bindings profiles camera tickets)
    add_test(NAME CoinRenderBumpShadow_${bump_shadow_mode}
      COMMAND CoinRenderBumpShadowTest --${bump_shadow_mode})
    set_tests_properties(CoinRenderBumpShadow_${bump_shadow_mode} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  foreach(bump_shadow_samples 2 4)
    add_test(NAME CoinRenderBumpShadow_SmokeMsaa${bump_shadow_samples}
      COMMAND CoinRenderBumpShadowTest --smoke --samples ${bump_shadow_samples})
    set_tests_properties(CoinRenderBumpShadow_SmokeMsaa${bump_shadow_samples} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

if(TARGET CoinRenderBumpShadowTest)
  foreach(bump_shadow_route staged direct)
    add_test(NAME CoinRenderBumpShadow_Rtt_${bump_shadow_route}
      COMMAND CoinRenderBumpShadowTest --rtt --${bump_shadow_route})
    set_tests_properties(CoinRenderBumpShadow_Rtt_${bump_shadow_route} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  if(X11_FOUND)
    add_executable(CoinRenderBumpShadowWindowTest CoinRenderBumpShadowWindowTest.cpp)
    target_compile_definitions(CoinRenderBumpShadowWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(CoinRenderBumpShadowWindowTest CoinRender Coin ${X11_LIBRARIES})
    target_include_directories(CoinRenderBumpShadowWindowTest PRIVATE
      ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src
      ${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
    foreach(bump_shadow_samples 1 2 4)
      add_test(NAME CoinRenderBumpShadow_Window${bump_shadow_samples}
        COMMAND CoinRenderBumpShadowWindowTest --samples ${bump_shadow_samples})
      set_tests_properties(CoinRenderBumpShadow_Window${bump_shadow_samples} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
    foreach(bump_shadow_route staged direct)
      add_test(NAME CoinRenderBumpShadow_WindowRtt_${bump_shadow_route}
        COMMAND CoinRenderBumpShadowWindowTest --rtt --${bump_shadow_route})
      set_tests_properties(CoinRenderBumpShadow_WindowRtt_${bump_shadow_route} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
  endif()
endif()

if(TARGET CoinRenderBumpShadowTest AND COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
  target_compile_definitions(CoinRenderBumpShadowTest PRIVATE COIN_BUMP_SHADOW_TEST_RUST)
  add_test(NAME CoinRenderBumpShadow_Raw COMMAND CoinRenderBumpShadowTest --raw)
  set_tests_properties(CoinRenderBumpShadow_Raw PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderBumpShadowTest)
  add_test(NAME CoinRenderBumpShadow_Srgb2D COMMAND CoinRenderBumpShadowTest --smoke --srgb)
  add_test(NAME CoinRenderBumpShadow_SrgbSpatialRefusal COMMAND CoinRenderBumpShadowTest --srgb-spatial-refusal)
  set_tests_properties(CoinRenderBumpShadow_Srgb2D CoinRenderBumpShadow_SrgbSpatialRefusal PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderBumpShadowTest)
  foreach(none_route staged direct)
    add_test(NAME CoinRenderBumpShadow_NoneAlpha_${none_route}
      COMMAND CoinRenderBumpShadowTest --rtt-none-alpha --${none_route})
    set_tests_properties(CoinRenderBumpShadow_NoneAlpha_${none_route} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

# Each focal is a fresh process: no prior ordinary control may warm its frame.
if(TARGET CoinRenderBumpShadowTest)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX")
    # This is a test-only requirement. Missing Python must not silently remove
    # the permanent unnecessary-program adversary while reporting CI success.
    find_package(Python3 3.7 REQUIRED COMPONENTS Interpreter)
  endif()
  foreach(cold_profile bump ordinary mixed)
    foreach(cold_samples 1 2 4)
      foreach(cold_policy native portable)
        set(cold_policy_args)
        if(cold_policy STREQUAL "portable")
          list(APPEND cold_policy_args --portable-sampling)
        endif()
        if(COIN_RENDER_BACKEND STREQUAL "BGFX")
          add_test(NAME CoinRenderBumpShadow_Cold_${cold_profile}_${cold_samples}_${cold_policy}
            COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/assert_bump_shadow_first_programs.py
              --profile ${cold_profile} --samples ${cold_samples} --
              $<TARGET_FILE:CoinRenderBumpShadowTest>
              --cold-${cold_profile} --samples ${cold_samples} ${cold_policy_args})
        else()
          add_test(NAME CoinRenderBumpShadow_Cold_${cold_profile}_${cold_samples}_${cold_policy}
            COMMAND CoinRenderBumpShadowTest --cold-${cold_profile} --samples ${cold_samples} ${cold_policy_args})
        endif()
        set_tests_properties(CoinRenderBumpShadow_Cold_${cold_profile}_${cold_samples}_${cold_policy}
          PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      endforeach()
    endforeach()
  endforeach()
endif()
if(TARGET CoinRenderBumpShadowWindowTest)
  add_test(NAME CoinRenderBumpShadow_ColdWindowPortable2
    COMMAND CoinRenderBumpShadowWindowTest --samples 2 --portable-sampling)
  set_tests_properties(CoinRenderBumpShadow_ColdWindowPortable2
    PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# C21.9 camera CROP and authorial shadow-map viewports. Focals share the
# existing fixture; old tests and their gates are unchanged.
if(TARGET CoinRenderShadowMapCameraTest)
  foreach(crop_mode geometry viewport raster state)
    add_test(NAME CoinRenderShadowMapCamera_Crop_${crop_mode}
      COMMAND CoinRenderShadowMapCameraTest --crop-${crop_mode})
    set_tests_properties(CoinRenderShadowMapCamera_Crop_${crop_mode} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  foreach(crop_executor general bump)
    add_test(NAME CoinRenderShadowMapCamera_Crop_${crop_executor}
      COMMAND CoinRenderShadowMapCameraTest --crop-viewport --crop-size 64 --crop-executor ${crop_executor})
    set_tests_properties(CoinRenderShadowMapCamera_Crop_${crop_executor} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  add_test(NAME CoinRenderShadowMapCamera_Crop_Canonical
    COMMAND CoinRenderShadowMapCameraTest --crop-geometry --canonical-raster)
  set_tests_properties(CoinRenderShadowMapCamera_Crop_Canonical PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# Signed orthographic near in every slot, including the private extension 5..8.
# FFI packing is linked privately into the existing test, never exported anew.
if(TARGET CoinRenderShadowMapCameraTest)
  target_sources(CoinRenderShadowMapCameraTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  foreach(near_mode near-slots near-slots-raster near-slots-state)
    foreach(near_executor fast general canonical)
      set(near_args --${near_mode})
      if(near_executor STREQUAL "general")
        list(APPEND near_args --near-general)
      elseif(near_executor STREQUAL "canonical")
        list(APPEND near_args --canonical-raster)
      endif()
      add_test(NAME CoinRenderShadowMapCamera_${near_mode}_${near_executor}
        COMMAND CoinRenderShadowMapCameraTest ${near_args})
      set_tests_properties(CoinRenderShadowMapCamera_${near_mode}_${near_executor}
        PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
  endforeach()
endif()

# C21 authored map cameras under actual MSAA, preserving the existing 1x gates.
if(TARGET CoinRenderShadowMapCameraTest)
  foreach(camera_mode near-slots near-slots-raster near-slots-state near-coverage near-crop)
    foreach(camera_samples 2 4)
      add_test(NAME CoinRenderCameraMsaa_${camera_mode}_${camera_samples}
        COMMAND CoinRenderShadowMapCameraTest --${camera_mode} --samples ${camera_samples})
      set_tests_properties(CoinRenderCameraMsaa_${camera_mode}_${camera_samples} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
  endforeach()
  foreach(camera_mode near-slots near-slots-state near-coverage near-crop)
    add_test(NAME CoinRenderCameraMsaa_${camera_mode}_General4
      COMMAND CoinRenderShadowMapCameraTest --${camera_mode} --samples 4 --near-general)
    set_tests_properties(CoinRenderCameraMsaa_${camera_mode}_General4 PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()
if(TARGET CoinRenderShadowMultitextureWindowTest AND TARGET CoinRenderShadowMapCameraTest)
  add_executable(CoinRenderShadowMapCameraWindowTest CoinRenderShadowMapCameraWindowTest.cpp)
  target_sources(CoinRenderShadowMapCameraWindowTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderShadowMapCameraWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderShadowMapCameraWindowTest CoinRender Coin ${X11_LIBRARIES})
  if(TARGET OpenGL::GL)
    target_link_libraries(CoinRenderShadowMapCameraWindowTest OpenGL::GL)
  else()
    target_link_libraries(CoinRenderShadowMapCameraWindowTest ${OPENGL_gl_LIBRARY})
  endif()
  target_include_directories(CoinRenderShadowMapCameraWindowTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(camera_window_mode geometry raster crop state)
    foreach(camera_samples 2 4)
      foreach(camera_readback gpu readback)
        set(camera_window_args --${camera_window_mode} --samples ${camera_samples})
        if(camera_readback STREQUAL "readback")
          list(APPEND camera_window_args --readback)
        endif()
        add_test(NAME CoinRenderCameraWindow_${camera_window_mode}_${camera_samples}_${camera_readback}
          COMMAND CoinRenderShadowMapCameraWindowTest ${camera_window_args})
        set_tests_properties(CoinRenderCameraWindow_${camera_window_mode}_${camera_samples}_${camera_readback} PROPERTIES
          TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      endforeach()
    endforeach()
  endforeach()
  add_test(NAME CoinRenderCameraWindow_GeneralPortable4
    COMMAND CoinRenderShadowMapCameraWindowTest --state --samples 4 --near-general --portable-sampling --readback)
  set_tests_properties(CoinRenderCameraWindow_GeneralPortable4 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# A real scissor intersection: guaranteed caster interiors on each side of
# the authored viewport edge, rather than a fully contained shape.
if(TARGET CoinRenderShadowMapCameraTest)
  add_test(NAME CoinRenderCameraMsaa_ScissorCrossing4
    COMMAND CoinRenderShadowMapCameraTest --near-scissor --samples 4 --near-maps 8)
  set_tests_properties(CoinRenderCameraMsaa_ScissorCrossing4 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()
if(TARGET CoinRenderShadowMapCameraWindowTest)
  add_test(NAME CoinRenderCameraWindow_ScissorCrossing4
    COMMAND CoinRenderShadowMapCameraWindowTest --scissor --samples 4 --near-maps 8 --readback)
  set_tests_properties(CoinRenderCameraWindow_ScissorCrossing4 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderShadowMapCameraTest)
  add_test(NAME CoinRenderShadowMapCamera_RestorationRules
    COMMAND CoinRenderShadowMapCameraTest --restore-rules)
  set_tests_properties(CoinRenderShadowMapCamera_RestorationRules PROPERTIES TIMEOUT 30)
endif()

# Stereo shadow-map cameras: first qualify opaque mono-observer offscreen1x.
# Parameters expose the remaining matrix to the serial executor without a
# compulsory Cartesian campaign or changes to the previous camera gates.
if(TARGET CoinRenderShadowMapCameraTest)
  foreach(stereo_light directional spot)
    foreach(stereo_camera perspective orthographic)
      foreach(stereo_maps 4 8)
        set(stereo_args --stereo --stereo-light ${stereo_light}
          --stereo-camera ${stereo_camera} --stereo-maps ${stereo_maps})
        if(stereo_maps EQUAL 8)
          list(APPEND stereo_args --stereo-general)
        endif()
        add_test(NAME CoinRenderCameraStereo_${stereo_light}_${stereo_camera}_${stereo_maps}
          COMMAND CoinRenderShadowMapCameraTest ${stereo_args})
        set_tests_properties(CoinRenderCameraStereo_${stereo_light}_${stereo_camera}_${stereo_maps}
          PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      endforeach()
    endforeach()
  endforeach()
  add_test(NAME CoinRenderCameraStereo_Crop8
    COMMAND CoinRenderShadowMapCameraTest --stereo --stereo-maps 8 --stereo-camera orthographic
      --stereo-mapping crop --stereo-general)
  add_test(NAME CoinRenderCameraStereo_Leave4
    COMMAND CoinRenderShadowMapCameraTest --stereo --stereo-maps 4 --stereo-light spot --stereo-mapping leave)
  add_test(NAME CoinRenderCameraStereo_Image4
    COMMAND CoinRenderShadowMapCameraTest --stereo-raster --stereo-maps 4 --stereo-raster-kind image)
  add_test(NAME CoinRenderCameraStereo_Marker8
    COMMAND CoinRenderShadowMapCameraTest --stereo-raster --stereo-maps 8 --stereo-light spot
      --stereo-camera orthographic --stereo-raster-kind marker --stereo-general)
  add_test(NAME CoinRenderCameraStereo_State4
    COMMAND CoinRenderShadowMapCameraTest --stereo-state --stereo-maps 4)
  add_test(NAME CoinRenderCameraStereo_State8
    COMMAND CoinRenderShadowMapCameraTest --stereo-state --stereo-maps 8 --stereo-camera orthographic
      --stereo-mapping crop --stereo-general)
  set_tests_properties(CoinRenderCameraStereo_Crop8 CoinRenderCameraStereo_Leave4
    CoinRenderCameraStereo_Image4 CoinRenderCameraStereo_Marker8
    CoinRenderCameraStereo_State4 CoinRenderCameraStereo_State8
    PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderShadowMapCameraTest)
  add_test(NAME CoinRenderCameraStereo_ScalesPerspective4
    COMMAND CoinRenderShadowMapCameraTest --stereo-scales --stereo-maps 4)
  add_test(NAME CoinRenderCameraStereo_ScalesOrthographic8
    COMMAND CoinRenderShadowMapCameraTest --stereo-scales --stereo-maps 8
      --stereo-camera orthographic --stereo-general)
  set_tests_properties(CoinRenderCameraStereo_ScalesPerspective4
    CoinRenderCameraStereo_ScalesOrthographic8 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# CPU-only authored-map camera inversion and real raster materialization gates.
if(TARGET CoinRenderShadowMapCameraTest)
  add_executable(CoinRenderShadowCameraInverseTest CoinRenderShadowCameraInverseTest.cpp)
  set_target_properties(CoinRenderShadowCameraInverseTest PROPERTIES
    COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
  target_link_libraries(CoinRenderShadowCameraInverseTest CoinRender Coin)
  target_include_directories(CoinRenderShadowCameraInverseTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderShadowCameraInverseCoreTest COMMAND CoinRenderShadowCameraInverseTest)
  set_tests_properties(CoinRenderShadowCameraInverseCoreTest PROPERTIES TIMEOUT 30)
endif()

# Stereo authored-map cameras with real Native MSAA and physical X11 delivery.
# The existing 1x/Canonical camera tests remain independent registrations.
function(coin_render_stereo_msaa_case name target)
  add_test(NAME ${name} COMMAND ${target} ${ARGN})
  set_tests_properties(${name} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
    RESOURCE_LOCK coinrender_gpu)
endfunction()
if(TARGET CoinRenderShadowMapCameraTest)
  coin_render_stereo_msaa_case(CoinRenderStereoMsaa_Geometry4_2
    CoinRenderShadowMapCameraTest --stereo-msaa --samples 2 --stereo-maps 4)
  coin_render_stereo_msaa_case(CoinRenderStereoMsaa_General8_4_Portable
    CoinRenderShadowMapCameraTest --stereo-msaa --samples 4 --stereo-maps 8
    --stereo-general --stereo-light spot --stereo-camera orthographic --portable-sampling)
  coin_render_stereo_msaa_case(CoinRenderStereoMsaa_Image4_2
    CoinRenderShadowMapCameraTest --stereo-msaa-raster --samples 2 --stereo-maps 4
    --stereo-light spot --stereo-raster-kind image)
  coin_render_stereo_msaa_case(CoinRenderStereoMsaa_Marker8_4
    CoinRenderShadowMapCameraTest --stereo-msaa-raster --samples 4 --stereo-maps 8
    --stereo-camera orthographic --stereo-raster-kind marker --stereo-general)
  coin_render_stereo_msaa_case(CoinRenderStereoMsaa_Crop8_4
    CoinRenderShadowMapCameraTest --stereo-msaa --samples 4 --stereo-maps 8
    --stereo-mapping crop --stereo-light spot --stereo-camera orthographic --stereo-general)
  coin_render_stereo_msaa_case(CoinRenderStereoMsaa_State4_2
    CoinRenderShadowMapCameraTest --stereo-msaa-state --samples 2 --stereo-maps 4)
  coin_render_stereo_msaa_case(CoinRenderStereoMsaa_State8_4
    CoinRenderShadowMapCameraTest --stereo-msaa-state --samples 4 --stereo-maps 8
    --stereo-slot 7 --stereo-camera orthographic --stereo-general --portable-sampling)
  coin_render_stereo_msaa_case(CoinRenderStereoMsaa_Scissor8_4
    CoinRenderShadowMapCameraTest --stereo-msaa-scissor --samples 4 --stereo-maps 8
    --stereo-general)
endif()
if(TARGET CoinRenderShadowMapCameraWindowTest)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_Geometry4_2_GPU
    CoinRenderShadowMapCameraWindowTest --stereo --geometry --samples 2 --stereo-maps 4)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_General8_4_Readback
    CoinRenderShadowMapCameraWindowTest --stereo --geometry --samples 4 --stereo-maps 8
    --stereo-slot 7 --stereo-general --portable-sampling --readback)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_Image4_4_GPU
    CoinRenderShadowMapCameraWindowTest --stereo --raster --samples 4 --stereo-maps 4
    --stereo-light spot --stereo-camera orthographic --stereo-raster-kind image)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_Marker8_2_Readback
    CoinRenderShadowMapCameraWindowTest --stereo --raster --samples 2 --stereo-maps 8
    --stereo-light spot --stereo-slot 7 --stereo-general --stereo-raster-kind marker --readback)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_Crop8_4_GPU
    CoinRenderShadowMapCameraWindowTest --stereo --crop --samples 4 --stereo-maps 8
    --stereo-slot 7 --stereo-general)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_Crop8_2_Readback
    CoinRenderShadowMapCameraWindowTest --stereo --crop --samples 2 --stereo-maps 8
    --stereo-slot 7 --stereo-light spot --stereo-camera orthographic --readback)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_State8_4_Readback
    CoinRenderShadowMapCameraWindowTest --stereo --state --samples 4 --stereo-maps 8
    --stereo-slot 7 --stereo-general --portable-sampling --readback)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_Shared4_2_GPU
    CoinRenderShadowMapCameraWindowTest --stereo-shared --geometry --samples 2 --stereo-maps 4
    --stereo-slot 2 --stereo-light spot --stereo-camera orthographic)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_SharedMarker8_4_Readback
    CoinRenderShadowMapCameraWindowTest --stereo-shared --raster --samples 4 --stereo-maps 8
    --stereo-slot 7 --stereo-camera orthographic --stereo-raster-kind marker --stereo-general --readback)
  coin_render_stereo_msaa_case(CoinRenderStereoWindow_Scissor8_4_GPU
    CoinRenderShadowMapCameraWindowTest --stereo --scissor --samples 4 --stereo-maps 8 --stereo-general)
endif()

# Normalization admission remains CPU-testable even when GPU jobs are busy.
if(TARGET CoinRender)
  add_executable(CoinRenderNormalizationTest CoinRenderNormalizationTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderNormalizationTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderNormalizationTest CoinRender Coin)
  target_include_directories(CoinRenderNormalizationTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderNormalizationCore COMMAND CoinRenderNormalizationTest --core)
  add_test(NAME CoinRenderNormalizationCpu COMMAND CoinRenderNormalizationTest --cpu)
  set_tests_properties(CoinRenderNormalizationCore CoinRenderNormalizationCpu PROPERTIES TIMEOUT 60)
  add_test(NAME CoinRenderNormalizationGpu COMMAND CoinRenderNormalizationTest --gpu)
  set_tests_properties(CoinRenderNormalizationGpu PROPERTIES
    TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# Native Annotation must keep its depth element synchronized with execution.
if(COIN_BUILD_LEGACY_GL_RENDERER)
  add_executable(CoinAnnotationDepthStateTest CoinAnnotationDepthStateTest.cpp)
  target_link_libraries(CoinAnnotationDepthStateTest Coin)
  if(TARGET OpenGL::GL)
    target_link_libraries(CoinAnnotationDepthStateTest OpenGL::GL)
  elseif(OPENGL_gl_LIBRARY)
    target_link_libraries(CoinAnnotationDepthStateTest ${OPENGL_gl_LIBRARY})
  endif()
  target_include_directories(CoinAnnotationDepthStateTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include)
  foreach(annotation_profile fields nested paths cache-policy)
    add_test(NAME CoinAnnotationDepth_${annotation_profile}
      COMMAND CoinAnnotationDepthStateTest --${annotation_profile})
    set_tests_properties(CoinAnnotationDepth_${annotation_profile} PROPERTIES
      TIMEOUT 120 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu
      ENVIRONMENT "COIN_GLX_PIXMAP_DIRECT_RENDERING=1")
  endforeach()
endif()

# Map-free planar raster fog has an independent pixel/anchor oracle. The font
# source below supplies immutable fixture data only, with test-local symbols.
if(TARGET CoinRender)
  add_executable(CoinRenderRasterPlanarFogTest CoinRenderRasterPlanarFogTest.cpp
    ${PROJECT_SOURCE_DIR}/src/fonts/default2dfont.cpp)
  target_compile_definitions(CoinRenderRasterPlanarFogTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H
    coin_default2dfont_get_height=coin_test_planar_font_height
    coin_default2dfont_get_width=coin_test_planar_font_width
    coin_default2dfont_get_bearing=coin_test_planar_font_bearing
    coin_default2dfont_get_data=coin_test_planar_font_data)
  target_link_libraries(CoinRenderRasterPlanarFogTest CoinRender Coin)
  target_include_directories(CoinRenderRasterPlanarFogTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderRasterPlanarFogCore COMMAND CoinRenderRasterPlanarFogTest --core)
  add_test(NAME CoinRenderRasterPlanarFogCpu COMMAND CoinRenderRasterPlanarFogTest --cpu)
  set_tests_properties(CoinRenderRasterPlanarFogCore CoinRenderRasterPlanarFogCpu PROPERTIES TIMEOUT 120)
  foreach(planar_kind image image-rgba text marker indexed)
    add_test(NAME CoinRenderRasterPlanarFogGpu_${planar_kind}
      COMMAND CoinRenderRasterPlanarFogTest --gpu --kind ${planar_kind})
    set_tests_properties(CoinRenderRasterPlanarFogGpu_${planar_kind} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

# Exercise optional saver connectors independently from the built-in encoders.
if(TARGET CoinRender AND UNIX)
  add_executable(CoinRenderLegacyCodecsTest CoinRenderLegacyCodecsTest.cpp)
  target_compile_definitions(CoinRenderLegacyCodecsTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderLegacyCodecsTest CoinRender Coin)
  target_include_directories(CoinRenderLegacyCodecsTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(legacy_codec jpeg gif sgi ps png tiff)
    add_test(NAME CoinRenderLegacyCodecsCpu_${legacy_codec}
      COMMAND CoinRenderLegacyCodecsTest --cpu --codec ${legacy_codec})
    set_tests_properties(CoinRenderLegacyCodecsCpu_${legacy_codec}
      PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
  endforeach()
  add_test(NAME CoinRenderLegacyCodecsGpu COMMAND CoinRenderLegacyCodecsTest --gpu)
  set_tests_properties(CoinRenderLegacyCodecsGpu PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRender AND (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_executable(CoinRenderBumpMultilightTest CoinRenderBumpMultilightTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderBumpMultilightTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderBumpMultilightTest CoinRender Coin)
  target_include_directories(CoinRenderBumpMultilightTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderBumpMultilightDelivery COMMAND CoinRenderBumpMultilightTest --mode delivery --lights 8 --maps 8)
  set_tests_properties(CoinRenderBumpMultilightDelivery PROPERTIES TIMEOUT 60)
  foreach(bump_lights 2 4 8)
    add_test(NAME CoinRenderBumpMultilightLighting${bump_lights}
      COMMAND CoinRenderBumpMultilightTest --mode lighting --lights ${bump_lights} --maps 0)
    set_tests_properties(CoinRenderBumpMultilightLighting${bump_lights} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  foreach(bump_mode maps textures state transform height mixed)
    add_test(NAME CoinRenderBumpMultilight_${bump_mode}
      COMMAND CoinRenderBumpMultilightTest --mode ${bump_mode} --lights 8 --maps 8)
    set_tests_properties(CoinRenderBumpMultilight_${bump_mode} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  add_test(NAME CoinRenderBumpMultilightHighMap COMMAND CoinRenderBumpMultilightTest --mode maps --lights 8 --maps 1 --portable-sampling)
  add_test(NAME CoinRenderBumpMultilightFourMaps COMMAND CoinRenderBumpMultilightTest --mode lighting --lights 8 --maps 4 --portable-sampling)
  set_tests_properties(CoinRenderBumpMultilightHighMap CoinRenderBumpMultilightFourMaps PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# C17.5 focals reuse the qualified C17.4 math, retaining every 1x registration.
if(TARGET CoinRenderBumpMultilightTest)
  function(coin_multilight_msaa_case name samples mode maps)
    add_test(NAME ${name} COMMAND CoinRenderBumpMultilightTest --samples ${samples}
      --mode ${mode} --lights 8 --maps ${maps} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endfunction()
  foreach(msaa_samples 2 4)
    coin_multilight_msaa_case(CoinRenderBumpMultilightMsaa${msaa_samples}Coverage ${msaa_samples} coverage 4)
    coin_multilight_msaa_case(CoinRenderBumpMultilightMsaa${msaa_samples}Lighting ${msaa_samples} lighting 0)
    coin_multilight_msaa_case(CoinRenderBumpMultilightMsaa${msaa_samples}Maps ${msaa_samples} maps 8)
    coin_multilight_msaa_case(CoinRenderBumpMultilightMsaa${msaa_samples}Mixed ${msaa_samples} mixed 4)
  endforeach()
  foreach(msaa_focus textures transform height state)
    coin_multilight_msaa_case(CoinRenderBumpMultilightMsaa4_${msaa_focus}_Portable 4 ${msaa_focus} 1 --portable-sampling)
  endforeach()
  if(UNIX AND NOT APPLE AND X11_FOUND)
    add_executable(CoinRenderBumpMultilightWindowTest CoinRenderBumpMultilightWindowTest.cpp
      ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
      ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
    target_compile_definitions(CoinRenderBumpMultilightWindowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(CoinRenderBumpMultilightWindowTest CoinRender Coin ${X11_LIBRARIES})
    target_include_directories(CoinRenderBumpMultilightWindowTest PRIVATE
      ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${X11_INCLUDE_DIR} ${COIN_TARGET_INCLUDE_DIRECTORIES})
    function(coin_multilight_window_case name samples maps)
      add_test(NAME ${name} COMMAND CoinRenderBumpMultilightWindowTest --samples ${samples}
        --lights 8 --maps ${maps} ${ARGN})
      set_tests_properties(${name} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endfunction()
    foreach(window_samples 1 2 4)
      coin_multilight_window_case(CoinRenderBumpMultilightWindow${window_samples}Coverage ${window_samples} 4 --coverage)
      coin_multilight_window_case(CoinRenderBumpMultilightWindow${window_samples}Readback ${window_samples} 4 --readback)
    endforeach()
    coin_multilight_window_case(CoinRenderBumpMultilightWindow4PortableCoverage 4 8 --coverage --readback --portable-sampling)
    coin_multilight_window_case(CoinRenderBumpMultilightWindow2PortableHighMap 2 1 --portable-sampling)
  endif()
endif()

# Explicit authorial depth states supplement the inherited default controls.
if(TARGET CoinRenderBumpMultilightTest)
  add_test(NAME CoinRenderBumpMultilightDeliveryLequal
    COMMAND CoinRenderBumpMultilightTest --mode delivery --lights 8 --maps 8 --lequal)
  set_tests_properties(CoinRenderBumpMultilightDeliveryLequal PROPERTIES TIMEOUT 60)
  coin_multilight_msaa_case(CoinRenderBumpMultilightMsaa4MixedLessPortable
    4 mixed 4 --less --portable-sampling)
  if(TARGET CoinRenderBumpMultilightWindowTest)
    coin_multilight_window_case(CoinRenderBumpMultilightWindow4LequalPortable
      4 8 --lequal --readback --portable-sampling)
  endif()
endif()

# Private-ingress masks retain the accepted FramePlan domain independently of
# public scene-node capture; existing C17.5 coverage focals remain unchanged.
if(TARGET CoinRenderBumpMultilightTest)
  add_executable(CoinRenderBumpMultilightMaskTest CoinRenderBumpMultilightMaskTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderBumpMultilightMaskTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderBumpMultilightMaskTest CoinRender Coin)
  target_include_directories(CoinRenderBumpMultilightMaskTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderBumpMultilightMaskCpu COMMAND CoinRenderBumpMultilightMaskTest --cpu)
  set_tests_properties(CoinRenderBumpMultilightMaskCpu PROPERTIES TIMEOUT 60)
  foreach(mask_samples 1 4)
    add_test(NAME CoinRenderBumpMultilightMaskGpu${mask_samples}
      COMMAND CoinRenderBumpMultilightMaskTest --gpu --samples ${mask_samples})
    set_tests_properties(CoinRenderBumpMultilightMaskGpu${mask_samples} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderBumpMultilightMaskTest PRIVATE COIN_MULTILIGHT_MASK_X11)
    target_link_libraries(CoinRenderBumpMultilightMaskTest ${X11_LIBRARIES})
    target_include_directories(CoinRenderBumpMultilightMaskTest PRIVATE ${X11_INCLUDE_DIR})
    add_test(NAME CoinRenderBumpMultilightMaskWindow
      COMMAND CoinRenderBumpMultilightMaskTest --window --samples 1)
    set_tests_properties(CoinRenderBumpMultilightMaskWindow PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# Legacy single-POINT bump masks use a private edited-FramePlan ingress.
if(TARGET CoinRenderBumpProfileTest)
  add_executable(CoinRenderBumpLegacyMaskTest CoinRenderBumpLegacyMaskTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderBumpLegacyMaskTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderBumpLegacyMaskTest CoinRender Coin)
  target_include_directories(CoinRenderBumpLegacyMaskTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderBumpLegacyMaskCpu COMMAND CoinRenderBumpLegacyMaskTest --cpu)
  set_tests_properties(CoinRenderBumpLegacyMaskCpu PROPERTIES TIMEOUT 60)
  foreach(legacy_mask_samples 1 2 4)
    add_test(NAME CoinRenderBumpLegacyMaskOff${legacy_mask_samples}
      COMMAND CoinRenderBumpLegacyMaskTest --gpu --samples ${legacy_mask_samples})
    set_tests_properties(CoinRenderBumpLegacyMaskOff${legacy_mask_samples} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderBumpLegacyMaskTest PRIVATE COIN_LEGACY_MASK_X11)
    target_link_libraries(CoinRenderBumpLegacyMaskTest ${X11_LIBRARIES})
    target_include_directories(CoinRenderBumpLegacyMaskTest PRIVATE ${X11_INCLUDE_DIR})
    foreach(legacy_mask_samples 1 2 4)
      add_test(NAME CoinRenderBumpLegacyMaskWindow${legacy_mask_samples}
        COMMAND CoinRenderBumpLegacyMaskTest --window --samples ${legacy_mask_samples})
      set_tests_properties(CoinRenderBumpLegacyMaskWindow${legacy_mask_samples} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
  endif()
endif()

# C17.7: public multilamp RTT with independently predicted producer/consumer.
if(TARGET CoinRenderBumpMultilightTest)
  add_executable(CoinRenderBumpMultilightRttTest CoinRenderBumpMultilightRttTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderBumpMultilightRttTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderBumpMultilightRttTest CoinRender Coin)
  target_include_directories(CoinRenderBumpMultilightRttTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderBumpMultilightRttCpu COMMAND CoinRenderBumpMultilightRttTest --cpu)
  set_tests_properties(CoinRenderBumpMultilightRttCpu PROPERTIES TIMEOUT 60)
  foreach(multirtt_route staged direct)
    foreach(multirtt_samples 1 2 4)
      if(multirtt_samples EQUAL 1)
        set(multirtt_lights 2)
        set(multirtt_maps 0)
        set(multirtt_sampling)
      elseif(multirtt_samples EQUAL 2)
        set(multirtt_lights 4)
        set(multirtt_maps 4)
        set(multirtt_sampling)
      else()
        set(multirtt_lights 8)
        set(multirtt_maps 8)
        set(multirtt_sampling --portable-sampling)
      endif()
      add_test(NAME CoinRenderBumpMultilightRtt_${multirtt_route}_Off${multirtt_samples}
        COMMAND CoinRenderBumpMultilightRttTest --gpu --mode smoke --${multirtt_route}
          --samples ${multirtt_samples} --lights ${multirtt_lights} --maps ${multirtt_maps} ${multirtt_sampling})
      set_tests_properties(CoinRenderBumpMultilightRtt_${multirtt_route}_Off${multirtt_samples}
        PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      if(UNIX AND NOT APPLE AND X11_FOUND)
        add_test(NAME CoinRenderBumpMultilightRtt_${multirtt_route}_Window${multirtt_samples}
          COMMAND CoinRenderBumpMultilightRttTest --window --mode smoke --${multirtt_route}
            --samples ${multirtt_samples} --lights ${multirtt_lights} --maps ${multirtt_maps} ${multirtt_sampling})
        set_tests_properties(CoinRenderBumpMultilightRtt_${multirtt_route}_Window${multirtt_samples}
          PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      endif()
    endforeach()
  endforeach()
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderBumpMultilightRttTest PRIVATE COIN_MULTILIGHT_RTT_X11)
    target_link_libraries(CoinRenderBumpMultilightRttTest ${X11_LIBRARIES})
    target_include_directories(CoinRenderBumpMultilightRttTest PRIVATE ${X11_INCLUDE_DIR})
  endif()
endif()

# Additional C17.7 update, transaction, and lifecycle focals.
if(TARGET CoinRenderBumpMultilightRttTest)
  foreach(multirtt_route staged direct)
    add_test(NAME CoinRenderBumpMultilightRtt_${multirtt_route}_Mutations
      COMMAND CoinRenderBumpMultilightRttTest --gpu --mode mutations --samples 1
        --lights 8 --maps 8 --${multirtt_route})
    add_test(NAME CoinRenderBumpMultilightRtt_${multirtt_route}_State
      COMMAND CoinRenderBumpMultilightRttTest --gpu --mode state --samples 4
        --lights 8 --maps 8 --${multirtt_route} --portable-sampling)
    set_tests_properties(CoinRenderBumpMultilightRtt_${multirtt_route}_Mutations
      CoinRenderBumpMultilightRtt_${multirtt_route}_State
      PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    if(UNIX AND NOT APPLE AND X11_FOUND)
      add_test(NAME CoinRenderBumpMultilightRtt_${multirtt_route}_Lifecycle
        COMMAND CoinRenderBumpMultilightRttTest --window --mode lifecycle --samples 2
          --lights 8 --maps 1 --${multirtt_route} --portable-sampling)
      set_tests_properties(CoinRenderBumpMultilightRtt_${multirtt_route}_Lifecycle
        PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endif()
  endforeach()
endif()

# Isolate source MSAA resolve with a constant texel and opaque source clear.
if(TARGET CoinRenderBumpMultilightRttTest)
  foreach(multirtt_route staged direct)
    foreach(multirtt_samples 2 4)
      add_test(NAME CoinRenderBumpMultilightRtt_${multirtt_route}_SourceCoverage${multirtt_samples}
        COMMAND CoinRenderBumpMultilightRttTest --gpu --mode coverage
          --samples ${multirtt_samples} --lights 8 --maps 8 --${multirtt_route})
      set_tests_properties(CoinRenderBumpMultilightRtt_${multirtt_route}_SourceCoverage${multirtt_samples}
        PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
    if(UNIX AND NOT APPLE AND X11_FOUND)
      add_test(NAME CoinRenderBumpMultilightRtt_${multirtt_route}_WindowSourceCoverage4
        COMMAND CoinRenderBumpMultilightRttTest --window --mode coverage
          --samples 4 --lights 8 --maps 8 --${multirtt_route} --portable-sampling)
      set_tests_properties(CoinRenderBumpMultilightRtt_${multirtt_route}_WindowSourceCoverage4
        PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endif()
  endforeach()
endif()

# B10.3c: actual depth-program selection and independent public output.
if(TARGET CoinRender AND UNIX AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_executable(CoinRenderTexture2dMsaaStartupTest CoinRenderTexture2dMsaaStartupTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxProgramSelection.cpp)
  target_compile_definitions(CoinRenderTexture2dMsaaStartupTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX")
    target_compile_definitions(CoinRenderTexture2dMsaaStartupTest PRIVATE COIN_TEXTURE2D_STARTUP_BGFX)
  elseif(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    target_compile_definitions(CoinRenderTexture2dMsaaStartupTest PRIVATE COIN_TEXTURE2D_STARTUP_WGPU)
  endif()
  target_link_libraries(CoinRenderTexture2dMsaaStartupTest CoinRender Coin)
  target_include_directories(CoinRenderTexture2dMsaaStartupTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderTexture2dMsaaStartupCpu COMMAND CoinRenderTexture2dMsaaStartupTest --cpu)
  set_tests_properties(CoinRenderTexture2dMsaaStartupCpu PROPERTIES TIMEOUT 60)
  foreach(texture2d_samples 1 2 4)
    add_test(NAME CoinRenderTexture2dMsaaStartupFirst${texture2d_samples}
      COMMAND CoinRenderTexture2dMsaaStartupTest --samples ${texture2d_samples} --mode first)
    set_tests_properties(CoinRenderTexture2dMsaaStartupFirst${texture2d_samples} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  add_test(NAME CoinRenderTexture2dMsaaStartupPortable4
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 4 --portable-sampling)
  add_test(NAME CoinRenderTexture2dMsaaStartupCanonical1
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 1 --canonical-raster)
  add_test(NAME CoinRenderTexture2dMsaaStartupPortableCanonical1
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 1 --canonical-raster --portable-sampling)
  add_test(NAME CoinRenderTexture2dMsaaStartupFallback4
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 4 --mode fallback)
  set_tests_properties(CoinRenderTexture2dMsaaStartupPortable4 CoinRenderTexture2dMsaaStartupCanonical1
    CoinRenderTexture2dMsaaStartupPortableCanonical1 CoinRenderTexture2dMsaaStartupFallback4
    PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# B10.3c incremental state interactions and admitted canonical MSAA variants.
if(TARGET CoinRenderTexture2dMsaaStartupTest)
  add_test(NAME CoinRenderTexture2dMsaaStartupCanonical2
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 2 --canonical-raster)
  add_test(NAME CoinRenderTexture2dMsaaStartupPortableCanonical4
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 4 --canonical-raster --portable-sampling)
  add_test(NAME CoinRenderTexture2dMsaaStartupStates4
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 4 --mode states --portable-sampling)
  set_tests_properties(CoinRenderTexture2dMsaaStartupCanonical2
    CoinRenderTexture2dMsaaStartupPortableCanonical4 CoinRenderTexture2dMsaaStartupStates4
    PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# B10.3c: first physical presentation/readback and independent target lifetime.
if(TARGET CoinRenderTexture2dMsaaStartupTest)
  add_test(NAME CoinRenderTexture2dMsaaStartupLifecycle2
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 2 --mode lifecycle)
  set_tests_properties(CoinRenderTexture2dMsaaStartupLifecycle2 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderTexture2dMsaaStartupTest PRIVATE COIN_TEXTURE2D_STARTUP_X11)
    target_include_directories(CoinRenderTexture2dMsaaStartupTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CoinRenderTexture2dMsaaStartupTest ${X11_LIBRARIES})
    foreach(texture2d_samples 1 2 4)
      add_test(NAME CoinRenderTexture2dMsaaStartupWindow${texture2d_samples}
        COMMAND CoinRenderTexture2dMsaaStartupTest --window --samples ${texture2d_samples})
      set_tests_properties(CoinRenderTexture2dMsaaStartupWindow${texture2d_samples} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
    add_test(NAME CoinRenderTexture2dMsaaStartupWindowLifecycle4
      COMMAND CoinRenderTexture2dMsaaStartupTest --window --samples 4 --mode lifecycle --portable-sampling)
    set_tests_properties(CoinRenderTexture2dMsaaStartupWindowLifecycle4 PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# B10.3c: qualified C17.7 bump producer and actual resolved 2D consumer family.
if(TARGET CoinRenderTexture2dMsaaStartupTest)
  add_executable(CoinRenderTexture2dMsaaRttStartupTest CoinRenderTexture2dMsaaRttStartupTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderTexture2dMsaaRttStartupTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX")
    target_compile_definitions(CoinRenderTexture2dMsaaRttStartupTest PRIVATE COIN_TEXTURE2D_STARTUP_BGFX)
  endif()
  target_link_libraries(CoinRenderTexture2dMsaaRttStartupTest CoinRender Coin)
  target_include_directories(CoinRenderTexture2dMsaaRttStartupTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(texture2d_route staged direct)
    add_test(NAME CoinRenderTexture2dMsaaStartupRtt_${texture2d_route}1
      COMMAND CoinRenderTexture2dMsaaRttStartupTest --${texture2d_route} --samples 1)
    set_tests_properties(CoinRenderTexture2dMsaaStartupRtt_${texture2d_route}1 PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  add_test(NAME CoinRenderTexture2dMsaaStartupRtt_Staged2
    COMMAND CoinRenderTexture2dMsaaRttStartupTest --staged --samples 2)
  add_test(NAME CoinRenderTexture2dMsaaStartupRtt_Direct4Portable
    COMMAND CoinRenderTexture2dMsaaRttStartupTest --direct --samples 4 --portable-sampling)
  set_tests_properties(CoinRenderTexture2dMsaaStartupRtt_Staged2
    CoinRenderTexture2dMsaaStartupRtt_Direct4Portable PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderTexture2dMsaaRttStartupTest PRIVATE COIN_MULTILIGHT_RTT_X11)
    target_include_directories(CoinRenderTexture2dMsaaRttStartupTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CoinRenderTexture2dMsaaRttStartupTest ${X11_LIBRARIES})
    add_test(NAME CoinRenderTexture2dMsaaStartupRtt_WindowStaged4
      COMMAND CoinRenderTexture2dMsaaRttStartupTest --window --staged --samples 4)
    add_test(NAME CoinRenderTexture2dMsaaStartupRtt_WindowDirect2Portable
      COMMAND CoinRenderTexture2dMsaaRttStartupTest --window --direct --samples 2 --portable-sampling)
    set_tests_properties(CoinRenderTexture2dMsaaStartupRtt_WindowStaged4
      CoinRenderTexture2dMsaaStartupRtt_WindowDirect2Portable PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# B10.3c: private accepted color masks are exercised at the real GPU boundary.
if(TARGET CoinRenderTexture2dMsaaStartupTest)
  add_test(NAME CoinRenderTexture2dMsaaStartupMasks4
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 4 --mode masks)
  set_tests_properties(CoinRenderTexture2dMsaaStartupMasks4 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    add_test(NAME CoinRenderTexture2dMsaaStartupWindowMasks2
      COMMAND CoinRenderTexture2dMsaaStartupTest --window --samples 2 --mode masks --portable-sampling)
    set_tests_properties(CoinRenderTexture2dMsaaStartupWindowMasks2 PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# One frame consumes both ordinary depth configurations, not just transitions.
if(TARGET CoinRenderTexture2dMsaaStartupTest)
  add_test(NAME CoinRenderTexture2dMsaaStartupDepthMix4
    COMMAND CoinRenderTexture2dMsaaStartupTest --samples 4 --mode depthmix --portable-sampling)
  set_tests_properties(CoinRenderTexture2dMsaaStartupDepthMix4 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# Shared shapes alternate ordinary2D/bump/ordinary2D with distinct materials.
if(TARGET CoinRenderTexture2dMsaaRttStartupTest)
  add_test(NAME CoinRenderTexture2dMsaaStartupMixedRuns4
    COMMAND CoinRenderTexture2dMsaaRttStartupTest --mixed --samples 4 --portable-sampling)
  set_tests_properties(CoinRenderTexture2dMsaaStartupMixedRuns4 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R22: typed public module contract and a public-only pre-R22 legacy control.
if(TARGET CoinRender)
  add_executable(CoinRenderPortableShaderModuleTest CoinRenderPortableShaderModuleTest.cpp)
  target_compile_definitions(CoinRenderPortableShaderModuleTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderPortableShaderModuleTest CoinRender Coin)
  target_include_directories(CoinRenderPortableShaderModuleTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderPortableShaderModuleCore COMMAND CoinRenderPortableShaderModuleTest --core)
  add_test(NAME CoinRenderPortableShaderModuleCpu COMMAND CoinRenderPortableShaderModuleTest --cpu)
  set_tests_properties(CoinRenderPortableShaderModuleCore CoinRenderPortableShaderModuleCpu PROPERTIES TIMEOUT 60)
  add_executable(CoinRenderPortableShaderLegacyTest CoinRenderPortableShaderLegacyTest.cpp)
  target_link_libraries(CoinRenderPortableShaderLegacyTest CoinRender Coin)
  target_include_directories(CoinRenderPortableShaderLegacyTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include ${COIN_TARGET_INCLUDE_DIRECTORIES})
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    foreach(portable_mode smoke ops precision uniforms)
      add_test(NAME CoinRenderPortableShaderModule_${portable_mode}
        COMMAND CoinRenderPortableShaderModuleTest --gpu --mode ${portable_mode})
      set_tests_properties(CoinRenderPortableShaderModule_${portable_mode} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
    add_test(NAME CoinRenderPortableShaderLegacy COMMAND CoinRenderPortableShaderLegacyTest --gpu)
    set_tests_properties(CoinRenderPortableShaderLegacy PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# R22 incremental: compiler reuse, scoped occurrences, and logical2D resources.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_test(NAME CoinRenderPortableShaderModule_scope
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode scope)
  add_test(NAME CoinRenderPortableShaderModule_textures
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode textures)
  add_test(NAME CoinRenderPortableShaderModule_texturesPortable
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode textures --portable-sampling)
  set_tests_properties(CoinRenderPortableShaderModule_scope CoinRenderPortableShaderModule_textures
    CoinRenderPortableShaderModule_texturesPortable PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R22: framework-owned stages and exact typed runtime binding validation.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  foreach(portable_mode owners alpha bindings)
    add_test(NAME CoinRenderPortableShaderModule_${portable_mode}
      COMMAND CoinRenderPortableShaderModuleTest --gpu --mode ${portable_mode})
    set_tests_properties(CoinRenderPortableShaderModule_${portable_mode} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

# R22: cold publication, real sample coverage, physical presentation and peers.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_test(NAME CoinRenderPortableShaderModule_samples2
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode presentation --samples 2)
  add_test(NAME CoinRenderPortableShaderModule_samples4Portable
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode presentation --samples 4 --portable-sampling)
  add_test(NAME CoinRenderPortableShaderModule_lifecycle2
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode lifecycle --samples 2)
  set_tests_properties(CoinRenderPortableShaderModule_samples2
    CoinRenderPortableShaderModule_samples4Portable CoinRenderPortableShaderModule_lifecycle2
    PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderPortableShaderModuleTest PRIVATE COIN_PORTABLE_MODULE_X11)
    target_include_directories(CoinRenderPortableShaderModuleTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CoinRenderPortableShaderModuleTest ${X11_LIBRARIES})
    foreach(module_samples 1 2 4)
      add_test(NAME CoinRenderPortableShaderModule_window${module_samples}
        COMMAND CoinRenderPortableShaderModuleTest --gpu --mode presentation --window --samples ${module_samples})
      set_tests_properties(CoinRenderPortableShaderModule_window${module_samples} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
  endif()
endif()

# R22: both module producer and module consumer execute on actual RTT routes.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  foreach(module_route direct staged)
    foreach(module_samples 1 4)
      add_test(NAME CoinRenderPortableShaderModule_rtt_${module_route}${module_samples}
        COMMAND CoinRenderPortableShaderModuleTest --gpu --mode rtt --${module_route} --samples ${module_samples})
      set_tests_properties(CoinRenderPortableShaderModule_rtt_${module_route}${module_samples} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endforeach()
  endforeach()
  if(UNIX AND NOT APPLE AND X11_FOUND)
    add_test(NAME CoinRenderPortableShaderModule_rtt_windowDirect2
      COMMAND CoinRenderPortableShaderModuleTest --gpu --mode rtt --direct --window --samples 2)
    add_test(NAME CoinRenderPortableShaderModule_rtt_windowStaged4Portable
      COMMAND CoinRenderPortableShaderModuleTest --gpu --mode rtt --staged --window --samples 4 --portable-sampling)
    set_tests_properties(CoinRenderPortableShaderModule_rtt_windowDirect2
      CoinRenderPortableShaderModule_rtt_windowStaged4Portable PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# R22 ABI63 transport remains pure CPU: no callback copy, context or device.
if(TARGET CoinRender)
  add_executable(CoinRenderPortableShaderTransportTest
    CoinRenderPortableShaderTransportTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderPortableShaderTransportTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderPortableShaderTransportTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderPortableShaderTransportTest CoinRender Coin)
  add_test(NAME CoinRenderPortableShaderTransportCpu COMMAND CoinRenderPortableShaderTransportTest)
  set_tests_properties(CoinRenderPortableShaderTransportCpu PROPERTIES TIMEOUT 60)
endif()

# R22 protocol63 uses the real DSO Core callback; never a test-local stub.
if(TARGET CoinRenderPortableShaderModuleTest AND COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
  target_compile_definitions(CoinRenderPortableShaderModuleTest PRIVATE COIN_PORTABLE_MODULE_RUST)
  target_sources(CoinRenderPortableShaderModuleTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  add_test(NAME CoinRenderPortableShaderModule_raw63
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode raw)
  add_test(NAME CoinRenderPortableShaderModule_raw63Portable4
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode raw --samples 4 --portable-sampling)
  set_tests_properties(CoinRenderPortableShaderModule_raw63
    CoinRenderPortableShaderModule_raw63Portable4 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R22: VSM remains a framework owner, including nonzero Phong specular.
if(TARGET CoinRenderPortableShaderModuleTest)
  if(NOT COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    target_sources(CoinRenderPortableShaderModuleTest PRIVATE
      ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
      ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  endif()
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    add_test(NAME CoinRenderPortableShaderModule_shadows
      COMMAND CoinRenderPortableShaderModuleTest --gpu --mode shadows)
    set_tests_properties(CoinRenderPortableShaderModule_shadows PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# R22/P07: homogeneous TEXCOORD precedes policy-dependent SAMPLE division.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_test(NAME CoinRenderPortableShaderModule_projection
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode projection)
  set_tests_properties(CoinRenderPortableShaderModule_projection PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R22: reuse the same analytic VSM/module focal for admitted MSAA and X11.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_test(NAME CoinRenderPortableShaderModule_shadows2
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode shadows --samples 2)
  add_test(NAME CoinRenderPortableShaderModule_shadows4Portable
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode shadows --samples 4 --portable-sampling)
  set_tests_properties(CoinRenderPortableShaderModule_shadows2
    CoinRenderPortableShaderModule_shadows4Portable PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    add_test(NAME CoinRenderPortableShaderModule_shadowsWindow1
      COMMAND CoinRenderPortableShaderModuleTest --gpu --mode shadows --window)
    add_test(NAME CoinRenderPortableShaderModule_shadowsWindow2Portable
      COMMAND CoinRenderPortableShaderModuleTest --gpu --mode shadows --window --samples 2 --portable-sampling)
    add_test(NAME CoinRenderPortableShaderModule_shadowsWindow4
      COMMAND CoinRenderPortableShaderModuleTest --gpu --mode shadows --window --samples 4)
    set_tests_properties(CoinRenderPortableShaderModule_shadowsWindow1
      CoinRenderPortableShaderModule_shadowsWindow2Portable
      CoinRenderPortableShaderModule_shadowsWindow4 PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# R22: frame-local sharing preserves the complete bit-exact 4KiB signature.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_test(NAME CoinRenderPortableShaderModule_interning
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode interning)
  set_tests_properties(CoinRenderPortableShaderModule_interning PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R22: real external compiler failure must precede DIRECT producer execution.
if(TARGET CoinRenderPortableShaderModuleTest AND COIN_RENDER_BACKEND STREQUAL "BGFX")
  target_compile_definitions(CoinRenderPortableShaderModuleTest PRIVATE COIN_PORTABLE_MODULE_BGFX)
  add_test(NAME CoinRenderPortableShaderModule_compilerFailure
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode compiler-failure --direct)
  set_tests_properties(CoinRenderPortableShaderModule_compilerFailure PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R22: explicit non-GPU executors must reject active modules before RTT work.
if(TARGET CoinRender)
  add_executable(CoinRenderPortableShaderCpuExecutionTest CoinRenderPortableShaderCpuExecutionTest.cpp)
  target_compile_definitions(CoinRenderPortableShaderCpuExecutionTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderPortableShaderCpuExecutionTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderPortableShaderCpuExecutionTest CoinRender Coin)
  add_test(NAME CoinRenderPortableShaderExplicitCpu COMMAND CoinRenderPortableShaderCpuExecutionTest)
  set_tests_properties(CoinRenderPortableShaderExplicitCpu PROPERTIES TIMEOUT 60)
endif()

# R22: mixed 1f/2f/3f dense packing and an observable 511/512 bank crossing.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_test(NAME CoinRenderPortableShaderModule_dense
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode dense)
  set_tests_properties(CoinRenderPortableShaderModule_dense PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R22: a fresh producer AST must fail compilation before earlier STAGED work.
if(TARGET CoinRenderPortableShaderModuleTest AND COIN_RENDER_BACKEND STREQUAL "BGFX")
  add_test(NAME CoinRenderPortableShaderModule_compilerProducer4
    COMMAND CoinRenderPortableShaderModuleTest --gpu --mode compiler-producer --samples 4 --staged)
  set_tests_properties(CoinRenderPortableShaderModule_compilerProducer4 PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R22: remaining operator/type/broadcast branches share one AST per width.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  foreach(portable_width 1 2 3 4)
    add_test(NAME CoinRenderPortableShaderModule_typed${portable_width}
      COMMAND CoinRenderPortableShaderModuleTest --gpu --mode typed${portable_width})
    set_tests_properties(CoinRenderPortableShaderModule_typed${portable_width} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

# R22 consumed-family preparation: the cold wrapper observes process startup,
# and the child always completes its independent image/Core/FFI transitions.
if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  if(COIN_RENDER_BACKEND STREQUAL "BGFX")
    find_package(Python3 3.7 REQUIRED COMPONENTS Interpreter)
    add_test(NAME CoinRenderPortablePreparationAuditCpu
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/assert_portable_preparation.py --self-test)
    set_tests_properties(CoinRenderPortablePreparationAuditCpu PROPERTIES TIMEOUT 60)
  endif()
  function(coin_add_portable_preparation name profile samples portable shown)
    set(preparation_args --gpu --mode prepare-${profile} --samples ${samples})
    if(portable)
      list(APPEND preparation_args --portable-sampling)
    endif()
    if(shown)
      list(APPEND preparation_args --window)
    endif()
    if(COIN_RENDER_BACKEND STREQUAL "BGFX")
      add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/assert_portable_preparation.py
        --profile ${profile} --samples ${samples} --
        $<TARGET_FILE:CoinRenderPortableShaderModuleTest> ${preparation_args})
    else()
      add_test(NAME ${name} COMMAND CoinRenderPortableShaderModuleTest ${preparation_args})
    endif()
    set_tests_properties(${name} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endfunction()
  coin_add_portable_preparation(CoinRenderPortablePreparationModule1 module 1 FALSE FALSE)
  coin_add_portable_preparation(CoinRenderPortablePreparationModule2Portable module 2 TRUE FALSE)
  coin_add_portable_preparation(CoinRenderPortablePreparationPrimary4Portable primary 4 TRUE FALSE)
  coin_add_portable_preparation(CoinRenderPortablePreparationMixed1 mixed 1 FALSE FALSE)
  coin_add_portable_preparation(CoinRenderPortablePreparationMixed4Portable mixed 4 TRUE FALSE)
  coin_add_portable_preparation(CoinRenderPortablePreparationOrdinary2 ordinary 2 FALSE FALSE)
  coin_add_portable_preparation(CoinRenderPortablePreparationShadow1 shadow 1 FALSE FALSE)
  coin_add_portable_preparation(CoinRenderPortablePreparationShadow4Portable shadow 4 TRUE FALSE)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    coin_add_portable_preparation(CoinRenderPortablePreparationWindow2Portable module 2 TRUE TRUE)
    coin_add_portable_preparation(CoinRenderPortablePreparationWindowShadow4 shadow 4 FALSE TRUE)
  endif()
endif()

# Simultaneous fixed legacy bump and adapted surface, sharing geometry.
if(COMMAND coin_add_portable_preparation)
  coin_add_portable_preparation(CoinRenderPortablePreparationBumpMixed1 bump-mixed 1 FALSE FALSE)
  coin_add_portable_preparation(CoinRenderPortablePreparationBumpMixed4Portable bump-mixed 4 TRUE FALSE)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    coin_add_portable_preparation(CoinRenderPortablePreparationWindowBumpMixed2 bump-mixed 2 FALSE TRUE)
  endif()
endif()

# R22.5 light requirements belong to the actual bump consumer, not its
# BASE_COLOR neighbor. Existing R22.4 PHONG preparations remain unchanged.
if(COMMAND coin_add_portable_preparation)
  add_test(NAME CoinRenderBumpOccurrenceCore
    COMMAND CoinRenderPortableShaderModuleTest --core --mode occurrence-core)
  set_tests_properties(CoinRenderBumpOccurrenceCore PROPERTIES TIMEOUT 60)
  coin_add_portable_preparation(CoinRenderBumpOccurrenceLegacy1 occurrence-legacy 1 FALSE FALSE)
  coin_add_portable_preparation(CoinRenderBumpOccurrenceLegacy4Portable occurrence-legacy 4 TRUE FALSE)
  coin_add_portable_preparation(CoinRenderBumpOccurrenceExtended2 occurrence-extended 2 FALSE FALSE)
  coin_add_portable_preparation(CoinRenderBumpOccurrenceExtended4Portable occurrence-extended 4 TRUE FALSE)
  coin_add_portable_preparation(CoinRenderBumpOccurrenceShadow1 occurrence-shadow 1 FALSE FALSE)
  coin_add_portable_preparation(CoinRenderBumpOccurrenceShadow4Portable occurrence-shadow 4 TRUE FALSE)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    coin_add_portable_preparation(CoinRenderBumpOccurrenceWindowLegacy2 occurrence-legacy 2 FALSE TRUE)
    coin_add_portable_preparation(CoinRenderBumpOccurrenceWindowShadow4Portable occurrence-shadow 4 TRUE TRUE)
  endif()
endif()

# R35.5/C25.5: native manager highlights share real RTT with their authored base.
if(TARGET CoinRender)
  add_executable(CoinRenderManagerHighlightRttTest CoinRenderManagerHighlightRttTest.cpp)
  target_compile_definitions(CoinRenderManagerHighlightRttTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderManagerHighlightRttTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderManagerHighlightRttTest CoinRender Coin)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX")
    target_compile_definitions(CoinRenderManagerHighlightRttTest PRIVATE COIN_HIGHLIGHT_RTT_BGFX)
  endif()
  add_test(NAME CoinRenderManagerHighlightRttCore COMMAND CoinRenderManagerHighlightRttTest --core)
  set_tests_properties(CoinRenderManagerHighlightRttCore PROPERTIES TIMEOUT 60)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    function(coin_add_manager_highlight_rtt name)
      add_test(NAME ${name} COMMAND CoinRenderManagerHighlightRttTest ${ARGN})
      set_tests_properties(${name} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endfunction()
    coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttBoxDirect1 --box --direct --samples 1)
    coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttLineStaged1 --line --staged --samples 1)
    coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttBoxAncestorStaged2 --box --ancestral --staged --samples 2)
    coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttLineAncestorDirect2 --line --ancestral --direct --samples 2)
    coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttBoxStaged4Portable --box --staged --samples 4 --portable-sampling)
    coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttLineAncestorDirect4Portable --line --ancestral --direct --samples 4 --portable-sampling)
  endif()
endif()

if(TARGET CoinRenderManagerHighlightRttTest AND UNIX AND NOT APPLE AND X11_FOUND)
  target_compile_definitions(CoinRenderManagerHighlightRttTest PRIVATE COIN_HIGHLIGHT_RTT_X11)
  target_include_directories(CoinRenderManagerHighlightRttTest PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(CoinRenderManagerHighlightRttTest ${X11_LIBRARIES})
  if(COMMAND coin_add_manager_highlight_rtt)
    coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttWindowBoxDirect2
      --window --box --ancestral --direct --samples 2)
    coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttWindowLineStaged4Portable
      --window --line --staged --samples 4 --portable-sampling)
  endif()
endif()

if(COMMAND coin_add_manager_highlight_rtt)
  coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttProducerControl --producer-control)
endif()

if(COMMAND coin_add_manager_highlight_rtt)
  coin_add_manager_highlight_rtt(CoinRenderManagerHighlightRttDirectProducerControl --producer-direct-control)
endif()

# R35.6/C25.6: manager highlight preserves authored VSM maps and shared paths.
if(TARGET CoinRender)
  add_executable(CoinRenderManagerHighlightShadowTest CoinRenderManagerHighlightShadowTest.cpp)
  target_compile_definitions(CoinRenderManagerHighlightShadowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderManagerHighlightShadowTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderManagerHighlightShadowTest CoinRender Coin)
  add_test(NAME CoinRenderManagerHighlightShadowCoreBox
    COMMAND CoinRenderManagerHighlightShadowTest --projected-witness --core --box --maps 1)
  add_test(NAME CoinRenderManagerHighlightShadowCoreLinePortable
    COMMAND CoinRenderManagerHighlightShadowTest --projected-witness --core --line --maps 4 --portable-sampling)
  set_tests_properties(CoinRenderManagerHighlightShadowCoreBox CoinRenderManagerHighlightShadowCoreLinePortable
    PROPERTIES TIMEOUT 60)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    function(coin_add_manager_highlight_shadow name)
      add_test(NAME ${name} COMMAND CoinRenderManagerHighlightShadowTest ${ARGN})
      set_tests_properties(${name} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endfunction()
    coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowBoxDir1 --projected-witness --box --maps 1 --samples 1)
    coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowLineDir2 --projected-witness --line --maps 4 --samples 2)
    coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowBoxDir4Portable --projected-witness --box --maps 8 --samples 4 --portable-sampling)
    coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowLineSpot1 --projected-witness --line --spot --maps 1 --samples 1)
    coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowBoxSpot2 --projected-witness --box --spot --maps 4 --samples 2)
    coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowLineSpot4Portable --projected-witness --line --spot --maps 8 --samples 4 --portable-sampling)
  endif()
endif()

# R35.6: ordinary lookalike state is authored, never an implicit replay marker.
if(TARGET CoinRenderManagerHighlightShadowTest)
  add_test(NAME CoinRenderManagerHighlightShadowCoreOrdinary
    COMMAND CoinRenderManagerHighlightShadowTest --core --line --maps 4 --projected-witness --ordinary-style)
  set_tests_properties(CoinRenderManagerHighlightShadowCoreOrdinary PROPERTIES TIMEOUT 60)
  if(COMMAND coin_add_manager_highlight_shadow)
    coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowOrdinaryLine2
      --line --maps 4 --samples 2 --projected-witness --ordinary-style)
    coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowNoReceive1
      --box --maps 1 --samples 1 --no-receive --predecessor-only)
  endif()
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderManagerHighlightShadowTest PRIVATE COIN_HIGHLIGHT_SHADOW_X11)
    target_include_directories(CoinRenderManagerHighlightShadowTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CoinRenderManagerHighlightShadowTest ${X11_LIBRARIES})
    if(COMMAND coin_add_manager_highlight_shadow)
      coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowWindowBoxDir2
        --window --box --maps 4 --samples 2 --projected-witness)
      coin_add_manager_highlight_shadow(CoinRenderManagerHighlightShadowWindowLineSpot4Portable
        --window --line --spot --maps 8 --samples 4 --portable-sampling --projected-witness)
    endif()
  endif()
endif()

# R35.6: a later ordinary foreground group must survive the highlight boundary.
if(TARGET CoinRenderManagerHighlightShadowTest)
  add_test(NAME CoinRenderManagerHighlightShadowCoreScopeBox
    COMMAND CoinRenderManagerHighlightShadowTest --core-scope --box --projected-witness)
  add_test(NAME CoinRenderManagerHighlightShadowCoreScopeLineEntry
    COMMAND CoinRenderManagerHighlightShadowTest --core-scope --line --ordinary-style --portable-sampling --projected-witness)
  set_tests_properties(CoinRenderManagerHighlightShadowCoreScopeBox CoinRenderManagerHighlightShadowCoreScopeLineEntry
    PROPERTIES TIMEOUT 60)
endif()

# B88: deterministic normal-array bounds before native indexed primitives.
if(TARGET CoinRender)
  add_executable(CoinRenderIndexedNormalBoundsTest CoinRenderIndexedNormalBoundsTest.cpp)
  target_compile_definitions(CoinRenderIndexedNormalBoundsTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderIndexedNormalBoundsTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderIndexedNormalBoundsTest CoinRender Coin)
  add_test(NAME CoinRenderIndexedNormalBoundsCore COMMAND CoinRenderIndexedNormalBoundsTest --core)
  add_test(NAME CoinRenderIndexedNormalBoundsCapture COMMAND CoinRenderIndexedNormalBoundsTest --capture)
  set_tests_properties(CoinRenderIndexedNormalBoundsCore CoinRenderIndexedNormalBoundsCapture PROPERTIES TIMEOUT 60)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    add_test(NAME CoinRenderIndexedNormalBoundsFaces COMMAND CoinRenderIndexedNormalBoundsTest --gpu)
    add_test(NAME CoinRenderIndexedNormalBoundsLines COMMAND CoinRenderIndexedNormalBoundsTest --gpu --lines)
    set_tests_properties(CoinRenderIndexedNormalBoundsFaces CoinRenderIndexedNormalBoundsLines
      PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# B88: actual subclass callbacks, effective-state edits and selected LINE replay.
if(TARGET CoinRenderIndexedNormalBoundsTest)
  add_test(NAME CoinRenderIndexedNormalBoundsExtended COMMAND CoinRenderIndexedNormalBoundsTest --extended)
  set_tests_properties(CoinRenderIndexedNormalBoundsExtended PROPERTIES TIMEOUT 60)
endif()

# B88: malformed RTT sources are rejected before either route publishes work.
if(TARGET CoinRenderIndexedNormalBoundsTest)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX")
    target_compile_definitions(CoinRenderIndexedNormalBoundsTest PRIVATE COIN_NORMAL_BOUNDS_BGFX)
  endif()
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    add_test(NAME CoinRenderIndexedNormalBoundsRttDirect COMMAND CoinRenderIndexedNormalBoundsTest --gpu --rtt --direct)
    add_test(NAME CoinRenderIndexedNormalBoundsRttStaged COMMAND CoinRenderIndexedNormalBoundsTest --gpu --rtt --staged)
    set_tests_properties(CoinRenderIndexedNormalBoundsRttDirect CoinRenderIndexedNormalBoundsRttStaged
      PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# B10.4: compact native Cube payload without suppressing human callbacks.
if(TARGET CoinRender)
  add_executable(CoinRenderCompactCubeCallbacksTest CoinRenderCompactCubeCallbacksTest.cpp)
  target_compile_definitions(CoinRenderCompactCubeCallbacksTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderCompactCubeCallbacksTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderCompactCubeCallbacksTest CoinRender Coin)
  add_test(NAME CoinRenderCompactCubePayload COMMAND CoinRenderCompactCubeCallbacksTest --compact)
  add_test(NAME CoinRenderCompactCubeObservers COMMAND CoinRenderCompactCubeCallbacksTest --observers)
  set_tests_properties(CoinRenderCompactCubePayload CoinRenderCompactCubeObservers PROPERTIES TIMEOUT 60)
endif()

# B10.4: native subclasses, primitive observers and raster state-pop boundaries.
if(TARGET CoinRenderCompactCubeCallbacksTest)
  add_test(NAME CoinRenderCompactCubeBoundaries COMMAND CoinRenderCompactCubeCallbacksTest --boundaries)
  set_tests_properties(CoinRenderCompactCubeBoundaries PROPERTIES TIMEOUT 60)
endif()

# R35.7/C25.7: real manager highlight, shared RTT and both VSM domains.
if(TARGET CoinRender)
  add_executable(CoinRenderManagerHighlightRttShadowTest CoinRenderManagerHighlightRttShadowTest.cpp)
  target_compile_definitions(CoinRenderManagerHighlightRttShadowTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderManagerHighlightRttShadowTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderManagerHighlightRttShadowTest CoinRender Coin)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX")
    target_compile_definitions(CoinRenderManagerHighlightRttShadowTest PRIVATE COIN_HIGHLIGHT_RTT_SHADOW_BGFX)
  endif()
  add_test(NAME CoinRenderManagerHighlightRttShadowCoreBox COMMAND CoinRenderManagerHighlightRttShadowTest --core --box --direct)
  add_test(NAME CoinRenderManagerHighlightRttShadowCoreLine COMMAND CoinRenderManagerHighlightRttShadowTest --core --line --staged)
  set_tests_properties(CoinRenderManagerHighlightRttShadowCoreBox CoinRenderManagerHighlightRttShadowCoreLine PROPERTIES TIMEOUT 60)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    foreach(kind Box Line)
      if(kind STREQUAL "Box")
        set(_highlight_rtt_shadow_kind --box)
      else()
        set(_highlight_rtt_shadow_kind --line)
      endif()
      foreach(route Direct Staged)
        if(route STREQUAL "Direct")
          set(_highlight_rtt_shadow_route --direct)
        else()
          set(_highlight_rtt_shadow_route --staged)
        endif()
        add_test(NAME CoinRenderManagerHighlightRttShadow${kind}${route}1
          COMMAND CoinRenderManagerHighlightRttShadowTest --gpu ${_highlight_rtt_shadow_kind} ${_highlight_rtt_shadow_route})
        set_tests_properties(CoinRenderManagerHighlightRttShadow${kind}${route}1
          PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
      endforeach()
    endforeach()
  endif()
endif()

# R35.7: proportional MSAA/map/spot/policy interactions and physical X11 resize.
if(TARGET CoinRenderManagerHighlightRttShadowTest)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    function(coin_add_combined_highlight name)
      add_test(NAME ${name} COMMAND CoinRenderManagerHighlightRttShadowTest --gpu ${ARGN})
      set_tests_properties(${name} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
    endfunction()
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowBoxDirect2Maps4 --box --direct --samples 2 --maps 4)
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowLineStaged4Maps8Portable --line --staged --samples 4 --maps 8 --portable-sampling)
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowLineDirect2Spot --line --direct --samples 2 --spot)
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowBoxStaged4SpotPortable --box --staged --samples 4 --spot --portable-sampling)
  endif()
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderManagerHighlightRttShadowTest PRIVATE COIN_HIGHLIGHT_SHADOW_X11)
    target_include_directories(CoinRenderManagerHighlightRttShadowTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CoinRenderManagerHighlightRttShadowTest ${X11_LIBRARIES})
    if(COMMAND coin_add_combined_highlight)
      coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowWindowBoxDirect1 --window --box --direct)
      coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowWindowLineStaged2 --window --line --staged --samples 2)
      coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowWindowBoxDirect4Portable --window --box --direct --samples 4 --portable-sampling)
    endif()
  endif()
endif()

# R35.7: same-frame purpose restoration and independent full-UV RTT coverage.
if(TARGET CoinRenderManagerHighlightRttShadowTest)
  add_test(NAME CoinRenderManagerHighlightRttShadowCoreScopeBox COMMAND CoinRenderManagerHighlightRttShadowTest --core-scope --box --direct)
  add_test(NAME CoinRenderManagerHighlightRttShadowCoreScopeLinePortable COMMAND CoinRenderManagerHighlightRttShadowTest --core-scope --line --staged --portable-sampling)
  add_test(NAME CoinRenderManagerHighlightRttShadowCoreSlots8Portable COMMAND CoinRenderManagerHighlightRttShadowTest --core --line --maps 8 --direct --portable-sampling)
  set_tests_properties(CoinRenderManagerHighlightRttShadowCoreScopeBox CoinRenderManagerHighlightRttShadowCoreScopeLinePortable CoinRenderManagerHighlightRttShadowCoreSlots8Portable PROPERTIES TIMEOUT 60)
  if(COMMAND coin_add_combined_highlight)
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowSourceDirect1 --producer-control --direct)
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowSourceDirect2 --producer-control --direct --samples 2)
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowSourceStaged4Portable --producer-control --staged --samples 4 --portable-sampling)
  endif()
endif()

# R35.7: consumed-domain admission and authored RTT mip/sampler delivery.
if(TARGET CoinRenderManagerHighlightRttShadowTest)
  add_test(NAME CoinRenderManagerHighlightRttShadowCoreIdle COMMAND CoinRenderManagerHighlightRttShadowTest --core-quality --line --direct)
  add_test(NAME CoinRenderManagerHighlightRttShadowCoreMips COMMAND CoinRenderManagerHighlightRttShadowTest --core --box --staged --mips --portable-sampling)
  set_tests_properties(CoinRenderManagerHighlightRttShadowCoreIdle CoinRenderManagerHighlightRttShadowCoreMips PROPERTIES TIMEOUT 60)
  if(COMMAND coin_add_combined_highlight)
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowBoxDirect4MipsPortable --box --direct --samples 4 --maps 4 --mips --portable-sampling)
    coin_add_combined_highlight(CoinRenderManagerHighlightRttShadowLineStaged2Mips --line --staged --samples 2 --mips)
  endif()
endif()

# CPU capture/overlay contract, including author callbacks and C++ subclasses.
if(COIN_BUILD_RENDER)
  add_executable(CoinRenderCubeOverlayCaptureTest CoinRenderCubeOverlayCaptureTest.cpp)
  target_compile_definitions(CoinRenderCubeOverlayCaptureTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderCubeOverlayCaptureTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinRenderCubeOverlayCaptureTest CoinRender Coin)
  foreach(mode incremental callbacks subclasses fallback drawstyle picking invalid materials notify transforms material-transitions)
    add_test(NAME CoinRenderCubeOverlay_${mode} COMMAND CoinRenderCubeOverlayCaptureTest --${mode})
    set_tests_properties(CoinRenderCubeOverlay_${mode} PROPERTIES TIMEOUT 30)
  endforeach()
endif()

if(TARGET CoinRenderCubeOverlayCaptureTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_test(NAME CoinRenderCubeOverlayGpu COMMAND CoinRenderCubeOverlayCaptureTest --gpu)
  set_tests_properties(CoinRenderCubeOverlayGpu PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# B87: preserve the functional 180-second deadline; bounded diagnosis and
# cleanup run afterwards. CTest's 195 seconds is only an outer failsafe.
if(TARGET CoinRenderTexture2dMsaaStartupTest AND UNIX AND NOT APPLE AND X11_FOUND)
  find_package(Python3 3.7 REQUIRED COMPONENTS Interpreter)
  add_test(NAME CoinRenderTexture2dMsaaStartupWindowMasks2ShutdownAudit
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/coinrender/run_with_timeout.py
      --timeout 180 --diagnostic-timeout 10
      --diagnostic-dir ${CMAKE_CURRENT_BINARY_DIR}/shutdown-audit-diagnostics --
      $<TARGET_FILE:CoinRenderTexture2dMsaaStartupTest>
      --window --samples 2 --mode masks --portable-sampling --audit-shutdown)
  set_tests_properties(CoinRenderTexture2dMsaaStartupWindowMasks2ShutdownAudit PROPERTIES
    TIMEOUT 195 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(COIN_BUILD_RENDER AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Python3 3.7 REQUIRED COMPONENTS Interpreter)
  add_test(NAME CoinRenderWatchdogContractCpu
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_run_with_timeout.py
      --runner ${PROJECT_SOURCE_DIR}/scripts/coinrender/run_with_timeout.py
      --temporary-root ${CMAKE_CURRENT_BINARY_DIR}/watchdog-contract-tmp)
  set_tests_properties(CoinRenderWatchdogContractCpu PROPERTIES TIMEOUT 120)
endif()

# C05.2: alpha on indexed/shared subclass contours and actual multi-object order.
if(TARGET CoinRenderConcaveAlphaTest)
  add_executable(CoinRenderConcaveIndexedAlphaTest CoinRenderConcaveIndexedAlphaTest.cpp)
  target_compile_definitions(CoinRenderConcaveIndexedAlphaTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderConcaveIndexedAlphaTest CoinRender Coin OpenGL::GL)
  target_include_directories(CoinRenderConcaveIndexedAlphaTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderConcaveIndexedAlphaAttributes COMMAND CoinRenderConcaveIndexedAlphaTest --attributes)
  add_test(NAME CoinRenderConcaveIndexedAlphaCpu COMMAND CoinRenderConcaveIndexedAlphaTest --cpu)
  set_tests_properties(CoinRenderConcaveIndexedAlphaAttributes CoinRenderConcaveIndexedAlphaCpu PROPERTIES TIMEOUT 120)
  add_test(NAME CoinRenderConcaveIndexedAlphaGpu COMMAND CoinRenderConcaveIndexedAlphaTest --gpu --portable-sampling)
  set_tests_properties(CoinRenderConcaveIndexedAlphaGpu PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderConcaveIndexedAlphaTest)
  add_test(NAME CoinRenderConcaveIndexedAlphaStatesCpu COMMAND CoinRenderConcaveIndexedAlphaTest --cpu --states)
  set_tests_properties(CoinRenderConcaveIndexedAlphaStatesCpu PROPERTIES TIMEOUT 120)
  add_test(NAME CoinRenderConcaveIndexedAlphaStatesGpu COMMAND CoinRenderConcaveIndexedAlphaTest --gpu --states --portable-sampling)
  set_tests_properties(CoinRenderConcaveIndexedAlphaStatesGpu PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

if(TARGET CoinRenderConcaveIndexedAlphaTest)
  add_test(NAME CoinRenderConcaveIndexedAlphaTransparentDestinationCpu COMMAND CoinRenderConcaveIndexedAlphaTest --cpu --transparent-destination)
  set_tests_properties(CoinRenderConcaveIndexedAlphaTransparentDestinationCpu PROPERTIES TIMEOUT 120)
endif()

# The finalization probe owns one complete isolated builder. Like
# CoinRenderFrameCoreTest, link its private implementation into the tests;
# share one object across fixture consumers without exporting production API.
if(TARGET CoinRenderImageNormalTexgenTest)
  add_library(CoinRenderNormalFinalizationObjects OBJECT
    ${PROJECT_SOURCE_DIR}/src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp)
  target_compile_definitions(CoinRenderNormalFinalizationObjects PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinRenderNormalFinalizationObjects PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(normal_fixture_target CoinRenderImageNormalTexgenTest CoinRenderImageNormalAntecedentTest
      CoinRenderImageNormalAntecedentGpuTest CoinRenderImageNormalTexgenGpuTest
      CoinRenderImageTexgenSubclassTest CoinRenderImageNormalShapeHistoryTest)
    if(TARGET ${normal_fixture_target})
      target_sources(${normal_fixture_target} PRIVATE $<TARGET_OBJECTS:CoinRenderNormalFinalizationObjects>)
    endif()
  endforeach()
endif()

# Whole public-frame descriptive bump measurement: one cold 32-frame sequence.
if(TARGET CoinRenderBumpProfileTest)
  add_executable(CoinRenderBumpTotalFrameTest CoinRenderBumpTotalFrameTest.cpp)
  target_sources(CoinRenderBumpTotalFrameTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderBumpTotalFrameTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_link_libraries(CoinRenderBumpTotalFrameTest CoinRender Coin)
  target_include_directories(CoinRenderBumpTotalFrameTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  add_test(NAME CoinRenderBumpTotalCapture COMMAND CoinRenderBumpTotalFrameTest --cpu)
  set_tests_properties(CoinRenderBumpTotalCapture PROPERTIES TIMEOUT 180)
  add_test(NAME CoinRenderBumpTotalOffscreen COMMAND CoinRenderBumpTotalFrameTest --offscreen)
  set_tests_properties(CoinRenderBumpTotalOffscreen PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  if(UNIX AND NOT APPLE AND X11_FOUND)
    target_compile_definitions(CoinRenderBumpTotalFrameTest PRIVATE COIN_BUMP_TOTAL_X11)
    target_include_directories(CoinRenderBumpTotalFrameTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CoinRenderBumpTotalFrameTest ${X11_LIBRARIES})
    add_test(NAME CoinRenderBumpTotalWindow COMMAND CoinRenderBumpTotalFrameTest --window)
    set_tests_properties(CoinRenderBumpTotalWindow PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endif()
endif()

# R28: independent numerical AA resolve/capture and baseline-manager control.
if(TARGET CoinRender)
  add_executable(CoinRenderMultipassTest CoinRenderMultipassTest.cpp)
  add_executable(CoinRenderMultipassLegacyManagerTest CoinRenderMultipassTest.cpp)
  target_compile_definitions(CoinRenderMultipassLegacyManagerTest PRIVATE COIN_MULTIPASS_LEGACY_MANAGER)
  foreach(_multipass_target CoinRenderMultipassTest CoinRenderMultipassLegacyManagerTest)
    target_compile_definitions(${_multipass_target} PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_include_directories(${_multipass_target} PRIVATE
      ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
    target_link_libraries(${_multipass_target} CoinRender Coin)
  endforeach()
  add_test(NAME CoinRenderMultipassApi COMMAND CoinRenderMultipassTest --core)
  add_test(NAME CoinRenderMultipassCpu COMMAND CoinRenderMultipassTest --cpu)
  add_test(NAME CoinRenderMultipassCpuManager COMMAND CoinRenderMultipassTest --cpu --manager --passes 4)
  set_tests_properties(CoinRenderMultipassApi CoinRenderMultipassCpu CoinRenderMultipassCpuManager PROPERTIES TIMEOUT 60)
  foreach(_multipass_count 2 3 4 7 8 16)
    add_test(NAME CoinRenderMultipassGpu${_multipass_count}
      COMMAND CoinRenderMultipassTest --gpu --passes ${_multipass_count})
    set_tests_properties(CoinRenderMultipassGpu${_multipass_count} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  add_test(NAME CoinRenderMultipassGpuManager4 COMMAND CoinRenderMultipassTest --gpu --manager --passes 4)
  set_tests_properties(CoinRenderMultipassGpuManager4 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: per-occurrence pixel viewport/camera delivery, with independent FFI proof.
if(TARGET CoinRenderMultipassTest)
  target_sources(CoinRenderMultipassTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  add_test(NAME CoinRenderMultipassCpuViewports COMMAND CoinRenderMultipassTest --cpu --viewports --passes 3)
  set_tests_properties(CoinRenderMultipassCpuViewports PROPERTIES TIMEOUT 60)
  add_test(NAME CoinRenderMultipassGpuViewports3 COMMAND CoinRenderMultipassTest --gpu --viewports --passes 3)
  set_tests_properties(CoinRenderMultipassGpuViewports3 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: one-apply physical multipass publication and same native target resize.
if(TARGET CoinRenderMultipassTest AND UNIX AND NOT APPLE AND X11_FOUND)
  target_compile_definitions(CoinRenderMultipassTest PRIVATE COIN_MULTIPASS_X11)
  target_include_directories(CoinRenderMultipassTest PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(CoinRenderMultipassTest ${X11_LIBRARIES})
  add_test(NAME CoinRenderMultipassWindow3 COMMAND CoinRenderMultipassTest --window --passes 3)
  set_tests_properties(CoinRenderMultipassWindow3 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: real M-sample resolve remains independent of N multipass accumulation.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassMsaa2N3 COMMAND CoinRenderMultipassTest --gpu --msaa --samples 2 --passes 3)
  add_test(NAME CoinRenderMultipassMsaa4N7 COMMAND CoinRenderMultipassTest --gpu --msaa --samples 4 --passes 7)
  set_tests_properties(CoinRenderMultipassMsaa2N3 CoinRenderMultipassMsaa4N7 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: real unique RTT producer consumed by both shared occurrences under N4.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassRttDirect4 COMMAND CoinRenderMultipassTest --rtt --direct --passes 4)
  add_test(NAME CoinRenderMultipassRttStaged4Portable COMMAND CoinRenderMultipassTest --rtt --staged --passes 4 --portable-sampling)
  set_tests_properties(CoinRenderMultipassRttDirect4 CoinRenderMultipassRttStaged4Portable PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: consumer jitter must not move VSM lookup/casters or truncate high maps.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassShadow1N4 COMMAND CoinRenderMultipassTest --shadows --maps 1 --passes 4)
  add_test(NAME CoinRenderMultipassShadow8N7Portable COMMAND CoinRenderMultipassTest --shadows --maps 8 --passes 7 --portable-sampling)
  set_tests_properties(CoinRenderMultipassShadow1N4 CoinRenderMultipassShadow8N7Portable PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: valid 1x-target multipass tickets survive refusal and owner destruction.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassTickets COMMAND CoinRenderMultipassTest --tickets)
  set_tests_properties(CoinRenderMultipassTickets PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: fog is evaluated per sample before averaging; clip stays in world space.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassCpuFogClip3 COMMAND CoinRenderMultipassTest --cpu --fog-clip --passes 3)
  set_tests_properties(CoinRenderMultipassCpuFogClip3 PROPERTIES TIMEOUT 60)
  add_test(NAME CoinRenderMultipassGpuFogClip4Portable COMMAND CoinRenderMultipassTest --gpu --fog-clip --passes 4 --portable-sampling)
  set_tests_properties(CoinRenderMultipassGpuFogClip4Portable PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28 native raster distinction: fixed Image; jitter-before-snap Marker.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassCpuRaster3 COMMAND CoinRenderMultipassTest --cpu --raster --passes 3)
  set_tests_properties(CoinRenderMultipassCpuRaster3 PROPERTIES TIMEOUT 60)
  add_test(NAME CoinRenderMultipassGpuRaster3Portable COMMAND CoinRenderMultipassTest --gpu --raster --passes 3 --portable-sampling)
  set_tests_properties(CoinRenderMultipassGpuRaster3Portable PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: portable output is evaluated per sample before fog, clipping and resolve.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassModule7Portable COMMAND CoinRenderMultipassTest --module --passes 7 --portable-sampling)
  set_tests_properties(CoinRenderMultipassModule7Portable PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: native mono Text2 snap/layout and mutation with an authored glyph oracle.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassCpuText2N3 COMMAND CoinRenderMultipassTest --cpu --text2 --passes 3)
  set_tests_properties(CoinRenderMultipassCpuText2N3 PROPERTIES TIMEOUT 60)
  add_test(NAME CoinRenderMultipassGpuText2N3Portable COMMAND CoinRenderMultipassTest --gpu --text2 --passes 3 --portable-sampling)
  set_tests_properties(CoinRenderMultipassGpuText2N3Portable PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: jitter uses each camera's cropped pixel viewport, in both occurrence orders.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassCpuCrop3 COMMAND CoinRenderMultipassTest --cpu --crop --passes 3)
  set_tests_properties(CoinRenderMultipassCpuCrop3 PROPERTIES TIMEOUT 60)
  add_test(NAME CoinRenderMultipassGpuCrop3 COMMAND CoinRenderMultipassTest --gpu --crop --passes 3)
  set_tests_properties(CoinRenderMultipassGpuCrop3 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: alpha-test/OIT/peeling composition before averaging, and live resources.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassCpuPeeling4 COMMAND CoinRenderMultipassTest --cpu --transparency --passes 4)
  set_tests_properties(CoinRenderMultipassCpuPeeling4 PROPERTIES TIMEOUT 60)
  foreach(_multipass_feature Peeling Weighted)
    if(_multipass_feature STREQUAL "Weighted")
      set(_multipass_transparency --weighted)
    else()
      set(_multipass_transparency --transparency)
    endif()
    add_test(NAME CoinRenderMultipassGpu${_multipass_feature}4 COMMAND CoinRenderMultipassTest --gpu ${_multipass_transparency} --passes 4)
    set_tests_properties(CoinRenderMultipassGpu${_multipass_feature}4 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  foreach(_multipass_route Direct Staged)
    if(_multipass_route STREQUAL "Direct")
      set(_multipass_route_arg --direct)
    else()
      set(_multipass_route_arg --staged)
    endif()
    add_test(NAME CoinRenderMultipassBudgetRtt${_multipass_route} COMMAND CoinRenderMultipassTest --budget ${_multipass_route_arg})
    set_tests_properties(CoinRenderMultipassBudgetRtt${_multipass_route} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
  add_test(NAME CoinRenderMultipassBudgetVsm COMMAND CoinRenderMultipassTest --budget --shadows)
  set_tests_properties(CoinRenderMultipassBudgetVsm PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: weighted OIT live MSAA attachments dominate its same-budget opaque control.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassWeightedMsaa4N2Budget COMMAND CoinRenderMultipassTest --gpu --weighted --msaa --samples 4 --passes 2)
  set_tests_properties(CoinRenderMultipassWeightedMsaa4N2Budget PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: first physical publication and resized window preserve real MSAA M/N.
if(TARGET CoinRenderMultipassTest AND UNIX AND NOT APPLE AND X11_FOUND)
  add_test(NAME CoinRenderMultipassWindowMsaa2N3 COMMAND CoinRenderMultipassTest --window --msaa --samples 2 --passes 3)
  add_test(NAME CoinRenderMultipassWindowMsaa4N7 COMMAND CoinRenderMultipassTest --window --msaa --samples 4 --passes 7)
  set_tests_properties(CoinRenderMultipassWindowMsaa2N3 CoinRenderMultipassWindowMsaa4N7 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: active normal-map delivery and sample-local layers/annotation depth.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassBump4 COMMAND CoinRenderMultipassTest --bump --passes 4)
  set_tests_properties(CoinRenderMultipassBump4 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  foreach(_multipass_layer Layers Annotations)
    if(_multipass_layer STREQUAL "Layers")
      set(_multipass_layer_arg --layers)
    else()
      set(_multipass_layer_arg --annotations)
    endif()
    add_test(NAME CoinRenderMultipassCpu${_multipass_layer}3 COMMAND CoinRenderMultipassTest --cpu ${_multipass_layer_arg} --passes 3)
    set_tests_properties(CoinRenderMultipassCpu${_multipass_layer}3 PROPERTIES TIMEOUT 60)
    add_test(NAME CoinRenderMultipassGpu${_multipass_layer}3 COMMAND CoinRenderMultipassTest --gpu ${_multipass_layer_arg} --passes 3)
    set_tests_properties(CoinRenderMultipassGpu${_multipass_layer}3 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
  endforeach()
endif()

# R28: real producer plus effective source/consumer VSM coexist under AA.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassRttVsmDirect3 COMMAND CoinRenderMultipassTest --rtt --shadows --direct --passes 3)
  add_test(NAME CoinRenderMultipassRttVsmStaged4Portable COMMAND CoinRenderMultipassTest --rtt --shadows --staged --passes 4 --portable-sampling)
  set_tests_properties(CoinRenderMultipassRttVsmDirect3 CoinRenderMultipassRttVsmStaged4Portable PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: native passUpdate remains admitted in the unchanged one-pass domain.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassCpuManager1 COMMAND CoinRenderMultipassTest --cpu --manager --passes 1)
  set_tests_properties(CoinRenderMultipassCpuManager1 PROPERTIES TIMEOUT 60)
endif()

# R28: fixed geometric palettes reject both forms of repeated8bit quantization.
if(TARGET CoinRenderMultipassTest)
  add_test(NAME CoinRenderMultipassCpuPrecision16 COMMAND CoinRenderMultipassTest --cpu --precision)
  set_tests_properties(CoinRenderMultipassCpuPrecision16 PROPERTIES TIMEOUT 60)
  add_test(NAME CoinRenderMultipassGpuPrecision16 COMMAND CoinRenderMultipassTest --gpu --precision)
  set_tests_properties(CoinRenderMultipassGpuPrecision16 PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu)
endif()

# R28: process shutdown must retire the complete wgpu runtime after ordinary
# device use and before pre-existing graphics-driver exit handlers. The child
# process keeps multiple targets, RTTs, readbacks and explicit devices alive;
# a pipe makes both sides of the atexit ordering observable without timing.
if(TARGET CoinRender AND COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"
   AND UNIX AND NOT ANDROID AND NOT EMSCRIPTEN)
  add_executable(CoinWgpuExitShutdownTest
    ../coinwgpu/CoinWgpuExitShutdownTest.cpp)
  set_target_properties(CoinWgpuExitShutdownTest PROPERTIES
    COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
  target_include_directories(CoinWgpuExitShutdownTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include
    ${PROJECT_BINARY_DIR}/src
    ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinWgpuExitShutdownTest CoinRender Coin)

  add_test(NAME CoinWgpuExitShutdown
    COMMAND CoinWgpuExitShutdownTest)
  add_test(NAME CoinWgpuExitShutdownResetReinit
    COMMAND CoinWgpuExitShutdownTest --reset-reinit)
  set_tests_properties(
    CoinWgpuExitShutdown
    CoinWgpuExitShutdownResetReinit
    PROPERTIES
      TIMEOUT 120
      SKIP_RETURN_CODE 77
      RESOURCE_LOCK coinrender_gpu
      LABELS "coinrender_gpu;wgpu_shutdown")
endif()

# R22 completion: transport envelopes and real aggregate logical resource cost.
if(TARGET CoinRender)
  foreach(completion_cpu Budget Envelope)
    add_executable(CoinRenderPortableShader${completion_cpu}Test CoinRenderPortableShader${completion_cpu}Test.cpp)
    target_compile_definitions(CoinRenderPortableShader${completion_cpu}Test PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(CoinRenderPortableShader${completion_cpu}Test CoinRender Coin)
    target_include_directories(CoinRenderPortableShader${completion_cpu}Test PRIVATE
      ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
    add_test(NAME CoinRenderPortableShader${completion_cpu}Cpu COMMAND CoinRenderPortableShader${completion_cpu}Test)
    set_tests_properties(CoinRenderPortableShader${completion_cpu}Cpu PROPERTIES TIMEOUT 60)
  endforeach()
  target_sources(CoinRenderPortableShaderBudgetTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    add_executable(CoinRenderPortableShaderRustEnvelopeTest CoinRenderPortableShaderRustEnvelopeTest.cpp)
    target_compile_definitions(CoinRenderPortableShaderRustEnvelopeTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(CoinRenderPortableShaderRustEnvelopeTest CoinRender Coin)
    target_include_directories(CoinRenderPortableShaderRustEnvelopeTest PRIVATE
      ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
    add_test(NAME CoinRenderPortableShaderRustEnvelopeCpu COMMAND CoinRenderPortableShaderRustEnvelopeTest)
    set_tests_properties(CoinRenderPortableShaderRustEnvelopeCpu PROPERTIES TIMEOUT 60)
  endif()
endif()

if(TARGET CoinRenderPortableShaderModuleTest AND
   (COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE"))
  add_executable(CoinRenderPortableShaderCompletionTest CoinRenderPortableShaderCompletionTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  target_compile_definitions(CoinRenderPortableShaderCompletionTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    target_compile_definitions(CoinRenderPortableShaderCompletionTest PRIVATE COIN_PORTABLE_MODULE_RUST)
  elseif(COIN_RENDER_BACKEND STREQUAL "BGFX")
    target_compile_definitions(CoinRenderPortableShaderCompletionTest PRIVATE COIN_PORTABLE_MODULE_BGFX)
  endif()
  target_link_libraries(CoinRenderPortableShaderCompletionTest CoinRender Coin)
  target_include_directories(CoinRenderPortableShaderCompletionTest PRIVATE
    ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  function(coin_portable_completion name)
    add_test(NAME CoinRenderPortableShaderCompletion_${name} COMMAND CoinRenderPortableShaderCompletionTest ${ARGN})
    set_tests_properties(CoinRenderPortableShaderCompletion_${name} PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu RUN_SERIAL TRUE)
  endfunction()
  coin_portable_completion(FormatsLinearNative --mode textures)
  coin_portable_completion(FormatsSrgbPortable --mode textures --srgb --portable-sampling)
  coin_portable_completion(Composition1 --mode composition)
  coin_portable_completion(Composition2 --mode composition --samples 2)
  coin_portable_completion(Composition4 --mode composition --samples 4)
  coin_portable_completion(ScreenDoor1 --mode screen)
  coin_portable_completion(ScreenDoor4 --mode screen --samples 4)
  coin_portable_completion(Weighted1 --mode composition --weighted)
  coin_portable_completion(Weighted4 --mode composition --weighted --samples 4)
  coin_portable_completion(Peeling1 --mode composition --peeling)
  coin_portable_completion(Peeling4 --mode composition --peeling --samples 4)
  coin_portable_completion(RttSrgbDirect --mode rtt-srgb --srgb --direct)
  coin_portable_completion(RttSrgbStaged --mode rtt-srgb --srgb --staged --portable-sampling)
  coin_portable_completion(BudgetDirect --mode budget --direct)
  coin_portable_completion(BudgetStaged --mode budget --staged)
endif()


# R23 completion: independent stored-sRGB volume arithmetic and public delivery.
if(TARGET CoinRender)
  add_executable(CoinRenderVolumeSrgbCoreTest CoinRenderVolumeSrgbCoreTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  add_executable(CoinRenderVolumeSrgbTest CoinRenderVolumeSrgbTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  foreach(_r23_target CoinRenderVolumeSrgbCoreTest CoinRenderVolumeSrgbTest)
    target_compile_definitions(${_r23_target} PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(${_r23_target} CoinRender Coin)
    target_include_directories(${_r23_target} PRIVATE
      ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  endforeach()
  if(TARGET OpenGL::GL)
    target_link_libraries(CoinRenderVolumeSrgbTest OpenGL::GL)
  else()
    target_link_libraries(CoinRenderVolumeSrgbTest ${OPENGL_gl_LIBRARY})
  endif()
  if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    target_compile_definitions(CoinRenderVolumeSrgbTest PRIVATE COIN_VOLUME_SRGB_RUST)
  endif()
  foreach(_r23_mode core transport limits)
    add_test(NAME CoinRenderVolumeSrgb_${_r23_mode} COMMAND CoinRenderVolumeSrgbCoreTest --${_r23_mode})
    set_tests_properties(CoinRenderVolumeSrgb_${_r23_mode} PROPERTIES TIMEOUT 60)
  endforeach()
  foreach(_r23_mode formats mips models composition raster refusal)
    add_test(NAME CoinRenderVolumeSrgbCpu_${_r23_mode} COMMAND CoinRenderVolumeSrgbTest --cpu --${_r23_mode})
    set_tests_properties(CoinRenderVolumeSrgbCpu_${_r23_mode} PROPERTIES TIMEOUT 60)
  endforeach()
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    function(coin_volume_srgb_gpu name target)
      add_test(NAME CoinRenderVolumeSrgb_${name} COMMAND ${target} ${ARGN})
      set_tests_properties(CoinRenderVolumeSrgb_${name} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu RUN_SERIAL TRUE)
    endfunction()
    foreach(_r23_mode formats mips models raster refusal)
      coin_volume_srgb_gpu(${_r23_mode}Native CoinRenderVolumeSrgbTest --${_r23_mode})
      coin_volume_srgb_gpu(${_r23_mode}Portable CoinRenderVolumeSrgbTest --${_r23_mode} --portable-sampling)
    endforeach()
    coin_volume_srgb_gpu(Composition1 CoinRenderVolumeSrgbTest --composition)
    coin_volume_srgb_gpu(Composition2Portable CoinRenderVolumeSrgbTest --composition --samples 2 --portable-sampling)
    coin_volume_srgb_gpu(Composition4 CoinRenderVolumeSrgbTest --composition --samples 4)
    coin_volume_srgb_gpu(Weighted4Portable CoinRenderVolumeSrgbTest --composition --weighted --samples 4 --portable-sampling)
    coin_volume_srgb_gpu(Peeling2 CoinRenderVolumeSrgbTest --composition --peeling --samples 2)
    if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
      coin_volume_srgb_gpu(RawIngress CoinRenderVolumeSrgbTest --raw)
    endif()
    coin_volume_srgb_gpu(RttDirect CoinRenderVolumeSrgbTest --rtt --direct)
    coin_volume_srgb_gpu(RttStagedPortable CoinRenderVolumeSrgbTest --rtt --portable-sampling)
    if(TARGET CoinRenderVolumeNpotRttTest)
      coin_volume_srgb_gpu(RttInheritedDirect CoinRenderVolumeNpotRttTest --srgb --direct)
      coin_volume_srgb_gpu(RttInheritedStagedPortable CoinRenderVolumeNpotRttTest --srgb --staged --portable-sampling)
    endif()
    if(TARGET CoinRenderShadowSpatialTest)
      coin_volume_srgb_gpu(Shadow1 CoinRenderShadowSpatialTest --srgb)
      coin_volume_srgb_gpu(Shadow4Portable CoinRenderShadowSpatialTest --srgb --samples 4 --portable-sampling)
    endif()
    if(TARGET CoinRenderVolumeNpotWindowTest)
      coin_volume_srgb_gpu(WindowReadback1 CoinRenderVolumeNpotWindowTest --srgb --readback)
      coin_volume_srgb_gpu(WindowReadback4Portable CoinRenderVolumeNpotWindowTest --srgb --readback --msaa4 --portable-sampling)
    endif()
  endif()
endif()

# Stored cubemap sRGB: independent author-byte, coordinate and admission oracles.
if(TARGET CoinRender)
  add_executable(CoinRenderCubeSrgbCoreTest CoinRenderCubeSrgbCoreTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  add_executable(CoinRenderCubeSrgbTest CoinRenderCubeSrgbTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuShadowFrame.cpp)
  foreach(_r24_target CoinRenderCubeSrgbCoreTest CoinRenderCubeSrgbTest)
    target_compile_definitions(${_r24_target} PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
    target_link_libraries(${_r24_target} CoinRender Coin)
    target_include_directories(${_r24_target} PRIVATE
      ${PROJECT_SOURCE_DIR}/testsuite/coinrender ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
  endforeach()
  if(TARGET OpenGL::GL)
    target_link_libraries(CoinRenderCubeSrgbTest OpenGL::GL)
  else()
    target_link_libraries(CoinRenderCubeSrgbTest ${OPENGL_gl_LIBRARY})
  endif()
  if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    target_compile_definitions(CoinRenderCubeSrgbTest PRIVATE COIN_CUBE_SRGB_RUST)
  endif()
  foreach(_r24_mode core transport limits)
    add_test(NAME CoinRenderCubeSrgb_${_r24_mode} COMMAND CoinRenderCubeSrgbCoreTest --${_r24_mode})
    set_tests_properties(CoinRenderCubeSrgb_${_r24_mode} PROPERTIES TIMEOUT 180)
  endforeach()
  foreach(_r24_mode formats coordinates mips models generators composition raster ingress)
    add_test(NAME CoinRenderCubeSrgbCpu_${_r24_mode} COMMAND CoinRenderCubeSrgbTest --cpu --${_r24_mode})
    set_tests_properties(CoinRenderCubeSrgbCpu_${_r24_mode} PROPERTIES TIMEOUT 90)
  endforeach()
  add_test(NAME CoinRenderCubeSrgbCpu_LinearControl COMMAND CoinRenderCubeSrgbTest --cpu --formats --linear)
  if(COIN_RENDER_BACKEND STREQUAL "BGFX" OR COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
    function(coin_cube_srgb_gpu name target)
      add_test(NAME CoinRenderCubeSrgb_${name} COMMAND ${target} ${ARGN})
      set_tests_properties(CoinRenderCubeSrgb_${name} PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 RESOURCE_LOCK coinrender_gpu RUN_SERIAL TRUE)
    endfunction()
    foreach(_r24_mode formats coordinates mips models generators raster ingress)
      coin_cube_srgb_gpu(${_r24_mode}Native CoinRenderCubeSrgbTest --${_r24_mode})
      coin_cube_srgb_gpu(${_r24_mode}Portable CoinRenderCubeSrgbTest --${_r24_mode} --portable-sampling)
    endforeach()
    coin_cube_srgb_gpu(LinearControl CoinRenderCubeSrgbTest --formats --linear)
    coin_cube_srgb_gpu(MaximumPortable CoinRenderCubeSrgbTest --maximum --portable-sampling)
    coin_cube_srgb_gpu(Composition1 CoinRenderCubeSrgbTest --composition)
    coin_cube_srgb_gpu(Composition2Portable CoinRenderCubeSrgbTest --composition --samples 2 --portable-sampling)
    coin_cube_srgb_gpu(Composition4 CoinRenderCubeSrgbTest --composition --samples 4)
    coin_cube_srgb_gpu(Weighted4Portable CoinRenderCubeSrgbTest --composition --weighted --samples 4 --portable-sampling)
    coin_cube_srgb_gpu(Peeling2 CoinRenderCubeSrgbTest --composition --peeling --samples 2)
    coin_cube_srgb_gpu(RttDirect CoinRenderCubeSrgbTest --rtt --direct)
    coin_cube_srgb_gpu(RttStagedPortable CoinRenderCubeSrgbTest --rtt --portable-sampling)
    if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
      coin_cube_srgb_gpu(RawIngress CoinRenderCubeSrgbTest --raw)
    endif()
    if(TARGET CoinRenderCubeRttTest)
      coin_cube_srgb_gpu(RttCubeLinearDirect CoinRenderCubeRttTest --srgb --transport --direct)
      coin_cube_srgb_gpu(RttCubeLinearStagedPortable CoinRenderCubeRttTest --srgb --transport --staged --portable-sampling)
    endif()
    if(TARGET CoinRenderShadowSpatialTest)
      coin_cube_srgb_gpu(Shadow1 CoinRenderShadowSpatialTest --srgb)
      coin_cube_srgb_gpu(Shadow4Portable CoinRenderShadowSpatialTest --srgb --samples 4 --portable-sampling)
    endif()
    if(TARGET CoinRenderVolumeNpotWindowTest)
      coin_cube_srgb_gpu(WindowReadback1 CoinRenderVolumeNpotWindowTest --cube --srgb --readback)
      coin_cube_srgb_gpu(WindowReadback4Portable CoinRenderVolumeNpotWindowTest --cube --srgb --readback --msaa4 --portable-sampling)
    endif()
  endif()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderCubeRttCompletionTests.cmake)

# R26 finite opaque legacy completion: independent arithmetic and actual factory delivery.
include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderBumpCompletionTests.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderMsaaAsyncCompletionTests.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderVrmlVisualTests.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderPortableApiTests.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderPortableContinuationTests.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderManagerMonoTests.cmake)

include(CoinRenderManagerStereoTests.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/CoinRenderHighlightCompletionTests.cmake)
