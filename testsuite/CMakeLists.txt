macro(create_testsuite input)
	get_filename_component(FLNAME "${input}" NAME_WE)
	get_filename_component(FLPATH "${input}" PATH)
	string(REGEX REPLACE ".*[/\\]" "" FLSUBFLD "${FLPATH}")
	set(COIN_STR_TEST_CLASS "${FLNAME}")
	file(READ ${PROJECT_SOURCE_DIR}/${input} f0)
	if(f0 MATCHES "#ifdef[ \t]+COIN_TEST_SUITE")
		# message(STATUS "Parse: ${PROJECT_SOURCE_DIR}/${input} - ${FLPATHSUB}${FLNAME}Test.cpp")
		# get first include from file, which we assume is include to tested class
		string(REGEX MATCH "[\n\r]+#include[ \t]<[^\n]+" iclass "${f0}")
		# get block between '#ifdef COIN_TEST_SUITE' and '#endif'
		string(REGEX REPLACE ".*#ifdef[ \t]+COIN_TEST_SUITE" "" f1 "${f0}")
		string(REGEX REPLACE "#endif[ \t/!]+COIN_TEST_SUITE.*" "" f2 "${f1}")
		# get all #include statements within COIN_TEST_SUITE code block
		string(REGEX MATCHALL "#include[ \t]<[^\n]+" i0 "${f2}")
		string(REPLACE ";" "\n" COIN_STR_TEST_INCL "${i0}")
		set(COIN_STR_TEST_INCL "${iclass}\n${COIN_STR_TEST_INCL}")
		# remove #include statements from test code string (moved to ${COIN_STR_TEST_INCL})
		string(REGEX REPLACE "[\n\r ]*#include[ \t]<[^\n]+" "" COIN_STR_TEST_CODE "${f2}")
		# generate new test code file with extracted snippets
		configure_file(TestSuiteTemplate.cmake.in "${FLSUBFLD}${FLNAME}Test.cpp")
	endif()
endmacro()

macro(create_file_test input testfunc filter)
	file(GLOB_RECURSE COIN_IV_FILES RELATIVE ${PROJECT_SOURCE_DIR}
		../${input}/*.iv
		../${input}/*.iv.gz
	)
	if (HAVE_VRML97)
		file(GLOB_RECURSE COIN_WRL_VRML_FILES RELATIVE ${PROJECT_SOURCE_DIR}
			../${input}/*.wrl
			../${input}/*.vrml
			../${input}/*.wrl.gz
			../${input}/*.vrml.gz
		)
		list(APPEND COIN_IV_FILES ${COIN_WRL_VRML_FILES})
	endif()
	string(REGEX REPLACE "[\\./\\-]+" "" TESTSUITENAME "${input}")
	set(COIN_STR_TEST_INCL "")
	set(COIN_STR_TEST_CODE "const char *readError[] = { \"Coin read error:\", 0 };\n")
	set(COIN_STR_TEST_CLASS "${TESTSUITENAME}")
	set(extra_macro_args ${ARGN})
	list(LENGTH extra_macro_args num_extra_args)
	foreach(INPUTFILE ${COIN_IV_FILES})
		if(${num_extra_args} EQUAL 0 OR NOT ("${INPUTFILE}" MATCHES "${extra_macro_args}"))
			string(REGEX REPLACE "[\\./\\-]+" "_" TESTNAME "${INPUTFILE}")
			if (${filter})
				set(COIN_STR_TEST_CODE "${COIN_STR_TEST_CODE}\nBOOST_AUTO_TEST_CASE(${TESTNAME}) { PushMessageSuppressFilters(readError); ResetReadErrorCount(); test_file(\"${PROJECT_SOURCE_DIR}/${INPUTFILE}\", &${testfunc}); PopMessageSuppressFilters(); }")
			else()
				set(COIN_STR_TEST_CODE "${COIN_STR_TEST_CODE}\nBOOST_AUTO_TEST_CASE(${TESTNAME}) { ResetReadErrorCount(); test_file(\"${PROJECT_SOURCE_DIR}/${INPUTFILE}\", &${testfunc}); }")
			endif()
		endif()
	endforeach()
	configure_file(TestSuiteTemplate.cmake.in "${TESTSUITENAME}Test.cpp")
endmacro()

# Parse all source files for embedded '#ifdef COIN_TEST_SUITE' and extract those blocks
# to separate test source files.
file(GLOB_RECURSE COIN_SRC_FILES RELATIVE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/*.cpp)
foreach(INPUTFILE ${COIN_SRC_FILES})
	create_testsuite(${INPUTFILE})
endforeach()

# Create test cases for all scenes under models folder (this replaces StandardTests.cpp).
# The original StandardTests.cpp also tested additional folders:
# - "killers" with testInCorrectFile
# - "slackers" with testOutOfSpecFile
# However, those folders are not in the repository.
create_file_test("models" "testCorrectFile" false "incorrect")
# bug039.iv and bug055-5.iv contain errors but load anyway: ignore for now
create_file_test("models/io/incorrect" "testIncorrectFile" true "(bug039|bug055-5)")

# Include all extracted '*Tests.cpp' files in the target.
FILE(GLOB COIN_TEST_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/*Test.cpp")

add_executable(CoinTests
	TestSuiteMain.cpp
	TestSuiteUtils.cpp
	TestSuiteMisc.cpp
	InternalMacroTest.cpp
	${COIN_TEST_SOURCES}
)
set_source_files_properties(InternalMacroTest.cpp PROPERTIES
	COMPILE_DEFINITIONS COIN_INTERNAL)
set_target_properties(CoinTests PROPERTIES DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}")
target_link_libraries(CoinTests Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(CoinTests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include/Inventor/annex
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
if(NOT WIN32)
	target_sources(CoinTests PRIVATE GLUWrapperTest.cpp GLUNurbsColorMapTest.cpp)
	target_include_directories(CoinTests PRIVATE ${PROJECT_BINARY_DIR}/src)
endif()
if (USE_PTHREAD)
	target_link_libraries(CoinTests pthread)
endif()
add_test(NAME CoinTests COMMAND CoinTests)

# This render regression requires a real GLX display; enable it explicitly in
# environments with X11 or run CTest under xvfb-run.
option(COIN_TEST_NURBS_COLOR_GLX "Enable NURBS control-color GLX render tests" OFF)
if(COIN_TEST_NURBS_COLOR_GLX)
  find_package(X11 REQUIRED)
  find_package(OpenGL REQUIRED)
  add_executable(NurbsControlColorRenderTest
    reproducers/nurbs-control-colors/repro.cpp)
  target_include_directories(NurbsControlColorRenderTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
    ${X11_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})
  target_link_libraries(NurbsControlColorRenderTest
    Coin ${X11_LIBRARIES} OpenGL::GL)
  add_test(NAME NurbsControlColorRender
    COMMAND NurbsControlColorRenderTest)
  add_test(NAME IndexedNurbsControlColorRender
    COMMAND NurbsControlColorRenderTest indexed)
  set_tests_properties(NurbsControlColorRender IndexedNurbsControlColorRender
    PROPERTIES TIMEOUT 30)
endif()

add_test(NAME CoinConfigCompatibility
  COMMAND ${CMAKE_COMMAND}
    -DCOIN_CONFIG_TEMPLATE=${PROJECT_SOURCE_DIR}/src/coin-config.cmake.in
    -DCOIN_COMPAT_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/coin-config-compatibility
    -P ${CMAKE_CURRENT_SOURCE_DIR}/CoinConfigCompatibilityTest.cmake)

# These public-API tests need fresh processes: CoinTests initializes SoDB
# before running its inline cases, which is too late to test startup options.
add_executable(CoinProfilerInitTest
  reproducers/soprofiler-initclass-ordering/repro.cpp)
target_link_libraries(CoinProfilerInitTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(CoinProfilerInitTest PRIVATE
  ${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
  ${COIN_TARGET_INCLUDE_DIRECTORIES})
foreach(profile_mode unset off 0 on 1 syncgl invalid)
  add_test(NAME ProfilerInit_${profile_mode}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_EXECUTABLE=$<TARGET_FILE:CoinProfilerInitTest>
      -DPROFILE_MODE=${profile_mode}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/reproducers/soprofiler-initclass-ordering/verify.cmake)
endforeach()

include(CheckCoreGLHeaders.cmake)

add_executable(RenderManagerFramePreparationTest RenderManagerFramePreparationTest.cpp)
target_link_libraries(RenderManagerFramePreparationTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RenderManagerFramePreparationTest PRIVATE
  ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include)
add_test(NAME RenderManagerFramePreparationTest COMMAND RenderManagerFramePreparationTest)

# Many warnings are generated from test macros on macOS with Xcode.
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Wtautological-compare DISABLE_WARNING_TAUTOLOGCOMPARE)
if(DISABLE_WARNING_TAUTOLOGCOMPARE)
	target_compile_options(CoinTests PUBLIC -Wno-tautological-compare)
endif()

option(COIN_TEST_WGPU_STRICT_DISPLAY "Enable strict X11 display test in CTest where missing DISPLAY is a failure" OFF)
option(COIN_TEST_WGPU_GL_REFERENCE "Enable strict Coin/GL offscreen lighting reference under Xvfb" OFF)

if(COIN_BUILD_WGPU)
	if((COIN_TEST_WGPU_STRICT_DISPLAY OR COIN_TEST_WGPU_GL_REFERENCE) AND
	   NOT COIN_WGPU_BACKEND STREQUAL "RUST_BRIDGE")
		message(FATAL_ERROR "Strict WebGPU display/GL reference tests require COIN_WGPU_BACKEND=RUST_BRIDGE")
	endif()
	add_executable(WgpuRenderActionTest
		actions/WgpuRenderActionTest.cpp
	)
	set_target_properties(WgpuRenderActionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuRenderActionTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuRenderActionTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuRenderActionTest COMMAND WgpuRenderActionTest)
	set_tests_properties(WgpuRenderActionTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuProductTest wgpu/WgpuProductTest.cpp)
	set_target_properties(WgpuProductTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuProductTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuProductTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuProductTest COMMAND WgpuProductTest)
	set_tests_properties(WgpuProductTest PROPERTIES TIMEOUT 30)

	# The same Coin-level contract runs against Rust/wgpu, BGFX and Recording
	# in separate builds. Backend-specific behavior remains in separate tests.
	add_executable(WgpuBackendContractTest wgpu/WgpuBackendContractTest.cpp)
	set_target_properties(WgpuBackendContractTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuBackendContractTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuBackendContractTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuBackendContractTest COMMAND WgpuBackendContractTest)
	set_tests_properties(WgpuBackendContractTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

	add_executable(WgpuOffscreenTest
		wgpu/WgpuOffscreenTest.cpp
	)
	set_target_properties(WgpuOffscreenTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuOffscreenTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuOffscreenTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuOffscreenTest COMMAND WgpuOffscreenTest)
	set_tests_properties(WgpuOffscreenTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuGateG0Test
		wgpu/WgpuGateG0Test.cpp
	)
	set_target_properties(WgpuGateG0Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuGateG0Test CoinWgpuExperimental Coin)
	target_include_directories(WgpuGateG0Test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuGateG0Test COMMAND WgpuGateG0Test)
	set_tests_properties(WgpuGateG0Test PROPERTIES TIMEOUT 30)

	add_executable(WgpuFrameCoreTest wgpu/WgpuFrameCoreTest.cpp)
	set_target_properties(WgpuFrameCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuFrameCoreTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuFrameCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuFrameCoreTest COMMAND WgpuFrameCoreTest)
	set_tests_properties(WgpuFrameCoreTest PROPERTIES TIMEOUT 30)

	if(COIN_WGPU_BACKEND STREQUAL "BGFX")
	add_executable(BgfxReadbackModesTest wgpu/BgfxReadbackModesTest.cpp)
	set_target_properties(BgfxReadbackModesTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(BgfxReadbackModesTest CoinWgpuExperimental Coin)
	target_include_directories(BgfxReadbackModesTest PRIVATE ${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(renderer vulkan opengl)
		add_test(NAME BgfxReadbackModes_${renderer} COMMAND ${CMAKE_COMMAND} -E env COIN_BGFX_RENDERER=${renderer} COIN_BGFX_TRANSPARENCY=auto $<TARGET_FILE:BgfxReadbackModesTest>)
		set_tests_properties(BgfxReadbackModes_${renderer} PROPERTIES TIMEOUT 90)
	endforeach()
	add_executable(BgfxSurfaceFeaturesTest wgpu/BgfxSurfaceFeaturesTest.cpp)
	set_target_properties(BgfxSurfaceFeaturesTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(BgfxSurfaceFeaturesTest CoinWgpuExperimental Coin)
	target_include_directories(BgfxSurfaceFeaturesTest PRIVATE ${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(renderer vulkan opengl)
		foreach(mode object weighted_oit sorted_layers)
			add_test(NAME BgfxSurface_${renderer}_${mode} COMMAND ${CMAKE_COMMAND} -E env COIN_BGFX_RENDERER=${renderer} COIN_BGFX_TRANSPARENCY=${mode} $<TARGET_FILE:BgfxSurfaceFeaturesTest>)
			set_tests_properties(BgfxSurface_${renderer}_${mode} PROPERTIES TIMEOUT 90)
		endforeach()
	endforeach()
	endif()
	add_executable(WgpuBgfxCoreTest wgpu/WgpuBgfxCoreTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/wgpu/SoWgpuBgfxCore.cpp)
	set_target_properties(WgpuBgfxCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuBgfxCoreTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuBgfxCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuBgfxCoreTest COMMAND WgpuBgfxCoreTest)
	add_executable(WgpuDepthContractTest wgpu/WgpuDepthContractTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/wgpu/SoWgpuBgfxCore.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/wgpu/SoWgpuFramePlanBuilder.cpp)
	target_sources(WgpuDepthContractTest PRIVATE ${PROJECT_SOURCE_DIR}/src/rendering/wgpu/SoWgpuFfiFrame.cpp)
	set_target_properties(WgpuDepthContractTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuDepthContractTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuDepthContractTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src)
	add_test(NAME WgpuDepthContractTest COMMAND WgpuDepthContractTest)
	set_tests_properties(WgpuBgfxCoreTest PROPERTIES TIMEOUT 30)
	if(COIN_WGPU_BACKEND STREQUAL "BGFX")
		add_test(NAME WgpuDepthContractVulkanTest COMMAND WgpuDepthContractTest --gpu)
		add_test(NAME WgpuDepthContractOpenGLTest COMMAND WgpuDepthContractTest --gpu)
		set_tests_properties(WgpuDepthContractVulkanTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=vulkan;COIN_BGFX_DISABLE_DRAW_GROUPING=1")
		set_tests_properties(WgpuDepthContractOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl;COIN_BGFX_DISABLE_DRAW_GROUPING=1")
		add_test(NAME WgpuProductOpenGLTest COMMAND WgpuProductTest)
		set_tests_properties(WgpuProductOpenGLTest PROPERTIES TIMEOUT 30
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_executable(WgpuBgfxOffscreenTest wgpu/WgpuBgfxOffscreenTest.cpp)
		set_target_properties(WgpuBgfxOffscreenTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(WgpuBgfxOffscreenTest CoinWgpuExperimental Coin)
		target_include_directories(WgpuBgfxOffscreenTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME WgpuBgfxOffscreenTest COMMAND WgpuBgfxOffscreenTest)
		set_tests_properties(WgpuBgfxOffscreenTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
		add_test(NAME WgpuBgfxOpenGLTest COMMAND WgpuBgfxOffscreenTest)
		set_tests_properties(WgpuBgfxOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_test(NAME WgpuBackendContractOpenGLTest COMMAND WgpuBackendContractTest)
		set_tests_properties(WgpuBackendContractOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_executable(WgpuBgfxTransparencyTest wgpu/WgpuBgfxTransparencyTest.cpp)
		set_target_properties(WgpuBgfxTransparencyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(WgpuBgfxTransparencyTest CoinWgpuExperimental Coin)
		target_include_directories(WgpuBgfxTransparencyTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME WgpuBgfxTransparencyTest COMMAND WgpuBgfxTransparencyTest)
		set_tests_properties(WgpuBgfxTransparencyTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
		add_test(NAME WgpuBgfxTransparencyOpenGLTest COMMAND WgpuBgfxTransparencyTest)
		set_tests_properties(WgpuBgfxTransparencyOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_test(NAME WgpuBgfxSortedLayersTest COMMAND WgpuBgfxTransparencyTest)
		set_tests_properties(WgpuBgfxSortedLayersTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_TRANSPARENCY=sorted_layers")
		add_test(NAME WgpuBgfxSortedLayersOpenGLTest COMMAND WgpuBgfxTransparencyTest)
		set_tests_properties(WgpuBgfxSortedLayersOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_TRANSPARENCY=sorted_layers;COIN_BGFX_RENDERER=opengl")
		add_test(NAME WgpuBgfxWeightedOitTest COMMAND WgpuBgfxTransparencyTest)
		set_tests_properties(WgpuBgfxWeightedOitTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_TRANSPARENCY=weighted_oit")
		add_test(NAME WgpuBgfxWeightedOitOpenGLTest COMMAND WgpuBgfxTransparencyTest)
		set_tests_properties(WgpuBgfxWeightedOitOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_TRANSPARENCY=weighted_oit;COIN_BGFX_RENDERER=opengl")
		if(X11_FOUND)
			add_executable(WgpuBgfxWindowTest wgpu/WgpuBgfxWindowTest.cpp)
			set_target_properties(WgpuBgfxWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(WgpuBgfxWindowTest CoinWgpuExperimental Coin ${X11_LIBRARIES})
			target_include_directories(WgpuBgfxWindowTest PRIVATE
				${PROJECT_SOURCE_DIR}/experimental/include
				${PROJECT_SOURCE_DIR}/include
				${PROJECT_SOURCE_DIR}/src
				${PROJECT_BINARY_DIR}/include
				${PROJECT_BINARY_DIR}/src
				${X11_INCLUDE_DIR}
				${COIN_TARGET_INCLUDE_DIRECTORIES}
			)
			add_test(NAME WgpuBgfxWindowTest COMMAND WgpuBgfxWindowTest)
			set_tests_properties(WgpuBgfxWindowTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
			add_test(NAME WgpuBgfxWindowOpenGLTest COMMAND WgpuBgfxWindowTest)
			set_tests_properties(WgpuBgfxWindowOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
			add_test(NAME WgpuBgfxRenderManagerAdapterTest COMMAND WgpuBgfxWindowTest)
			set_tests_properties(WgpuBgfxRenderManagerAdapterTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_TEST_RENDER_MANAGER_ADAPTER=1")
			add_test(NAME WgpuBgfxRenderManagerAdapterOpenGLTest COMMAND WgpuBgfxWindowTest)
			set_tests_properties(WgpuBgfxRenderManagerAdapterOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_TEST_RENDER_MANAGER_ADAPTER=1;COIN_BGFX_RENDERER=opengl")
			add_executable(WgpuBgfxMultiWindowTest wgpu/WgpuBgfxMultiWindowTest.cpp)
			set_target_properties(WgpuBgfxMultiWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(WgpuBgfxMultiWindowTest CoinWgpuExperimental Coin ${X11_LIBRARIES})
			get_target_property(_bgfx_window_includes WgpuBgfxWindowTest INCLUDE_DIRECTORIES)
			target_include_directories(WgpuBgfxMultiWindowTest PRIVATE ${_bgfx_window_includes})
			add_test(NAME WgpuBgfxMultiWindowTest COMMAND WgpuBgfxMultiWindowTest)
			set_tests_properties(WgpuBgfxMultiWindowTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
			add_test(NAME WgpuBgfxMultiWindowOpenGLTest COMMAND WgpuBgfxMultiWindowTest)
			set_tests_properties(WgpuBgfxMultiWindowOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
			add_test(NAME WgpuBgfxMultiWindowOffscreenFirstTest COMMAND WgpuBgfxMultiWindowTest)
			set_tests_properties(WgpuBgfxMultiWindowOffscreenFirstTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_TEST_OFFSCREEN_FIRST=1")
			add_test(NAME WgpuBgfxMultiWindowOffscreenFirstOpenGLTest COMMAND WgpuBgfxMultiWindowTest)
			set_tests_properties(WgpuBgfxMultiWindowOffscreenFirstOpenGLTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_TEST_OFFSCREEN_FIRST=1;COIN_BGFX_RENDERER=opengl")
			foreach(_composition weighted_oit sorted_layers)
				foreach(_renderer vulkan opengl)
					set(_multi_composition_test "WgpuBgfxMultiWindow_${_composition}_${_renderer}Test")
					add_test(NAME ${_multi_composition_test} COMMAND WgpuBgfxMultiWindowTest)
					set_tests_properties(${_multi_composition_test} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
						ENVIRONMENT "COIN_BGFX_TRANSPARENCY=${_composition};COIN_BGFX_RENDERER=${_renderer}")
				endforeach()
			endforeach()
		endif()
	endif()

	add_executable(WgpuFrameReuseCoreTest wgpu/WgpuFrameReuseCoreTest.cpp)
	set_target_properties(WgpuFrameReuseCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuFrameReuseCoreTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuFrameReuseCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuFrameReuseCoreTest COMMAND WgpuFrameReuseCoreTest)
	set_tests_properties(WgpuFrameReuseCoreTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuDiagnosticShellTest wgpu/WgpuDiagnosticShellTest.cpp)
	set_target_properties(WgpuDiagnosticShellTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuDiagnosticShellTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuDiagnosticShellTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuDiagnosticShellTest COMMAND WgpuDiagnosticShellTest)
	set_tests_properties(WgpuDiagnosticShellTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuIndexedGeometryCoreTest wgpu/WgpuIndexedGeometryCoreTest.cpp)
	set_target_properties(WgpuIndexedGeometryCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuIndexedGeometryCoreTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuIndexedGeometryCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuIndexedGeometryCoreTest COMMAND WgpuIndexedGeometryCoreTest)
	set_tests_properties(WgpuIndexedGeometryCoreTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuGateG1Test
		wgpu/WgpuGateG1Test.cpp
	)
	set_target_properties(WgpuGateG1Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuGateG1Test CoinWgpuExperimental Coin)
	target_include_directories(WgpuGateG1Test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuGateG1Test COMMAND WgpuGateG1Test)
	set_tests_properties(WgpuGateG1Test PROPERTIES TIMEOUT 30)

	add_executable(WgpuGateG2Test
		wgpu/WgpuGateG2Test.cpp
	)
	set_target_properties(WgpuGateG2Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuGateG2Test CoinWgpuExperimental Coin)
	target_include_directories(WgpuGateG2Test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuGateG2Test COMMAND WgpuGateG2Test)
	set_tests_properties(WgpuGateG2Test PROPERTIES TIMEOUT 30)

	add_executable(WgpuIndexedFastPathTest
		wgpu/WgpuIndexedFastPathTest.cpp
	)
	set_target_properties(WgpuIndexedFastPathTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuIndexedFastPathTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuIndexedFastPathTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuIndexedFastPathTest COMMAND WgpuIndexedFastPathTest)
	set_tests_properties(WgpuIndexedFastPathTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuCacheTest
		wgpu/WgpuCacheTest.cpp
	)
	set_target_properties(WgpuCacheTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuCacheTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuCacheTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuCacheTest COMMAND WgpuCacheTest)
	set_tests_properties(WgpuCacheTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuStabilizationTest
		wgpu/WgpuStabilizationTest.cpp
	)
	set_target_properties(WgpuStabilizationTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuStabilizationTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuStabilizationTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuStabilizationTest COMMAND WgpuStabilizationTest)
	set_tests_properties(WgpuStabilizationTest PROPERTIES TIMEOUT 30)

	if(COIN_WGPU_BACKEND STREQUAL "RUST_BRIDGE")
	add_executable(WgpuMaterialTest
		wgpu/WgpuMaterialTest.cpp
	)
	set_target_properties(WgpuMaterialTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuMaterialTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuMaterialTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuMaterialTest COMMAND WgpuMaterialTest)
	set_tests_properties(WgpuMaterialTest PROPERTIES TIMEOUT 30)
	endif()

	add_executable(WgpuTextureTest
		wgpu/WgpuTextureTest.cpp
	)
	set_target_properties(WgpuTextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuTextureTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuTextureTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuTextureTest COMMAND WgpuTextureTest)
	set_tests_properties(WgpuTextureTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuLightingTest
		wgpu/WgpuLightingTest.cpp
	)
	set_target_properties(WgpuLightingTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuLightingTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuLightingTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuLightingTest COMMAND WgpuLightingTest)
	set_tests_properties(WgpuLightingTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuGouraudInterpolationTest
		wgpu/WgpuGouraudInterpolationTest.cpp
	)
	set_target_properties(WgpuGouraudInterpolationTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuGouraudInterpolationTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuGouraudInterpolationTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuGouraudInterpolationTest COMMAND WgpuGouraudInterpolationTest)
	set_tests_properties(WgpuGouraudInterpolationTest PROPERTIES TIMEOUT 30)
	if(COIN_WGPU_BACKEND STREQUAL "BGFX")
		foreach(_gouraud_renderer vulkan opengl)
			foreach(_gouraud_mode object weighted_oit sorted_layers)
				add_test(NAME WgpuGouraud_${_gouraud_renderer}_${_gouraud_mode}
					COMMAND WgpuGouraudInterpolationTest)
				set_tests_properties(WgpuGouraud_${_gouraud_renderer}_${_gouraud_mode}
					PROPERTIES TIMEOUT 30 ENVIRONMENT
					"COIN_BGFX_RENDERER=${_gouraud_renderer};COIN_BGFX_TRANSPARENCY=${_gouraud_mode}")
			endforeach()
		endforeach()
	endif()

	add_executable(WgpuFogTest
		wgpu/WgpuFogTest.cpp
	)
	set_target_properties(WgpuFogTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuFogTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuFogTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuFogTest COMMAND WgpuFogTest)
	set_tests_properties(WgpuFogTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuCompositionTest wgpu/WgpuCompositionTest.cpp)
	set_target_properties(WgpuCompositionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuCompositionTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuCompositionTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuCompositionTest COMMAND WgpuCompositionTest)
	set_tests_properties(WgpuCompositionTest PROPERTIES TIMEOUT 30)
	if(COIN_WGPU_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME WgpuAnnotationTest COMMAND WgpuCompositionTest --annotations)
		set_tests_properties(WgpuAnnotationTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
	endif()

	add_executable(WgpuSceneTextureTest wgpu/WgpuSceneTextureTest.cpp)
	set_target_properties(WgpuSceneTextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuSceneTextureTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuSceneTextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuSceneTextureTest COMMAND WgpuSceneTextureTest)
	set_tests_properties(WgpuSceneTextureTest PROPERTIES TIMEOUT 30)

	add_executable(WgpuSceneTextureBudgetTest wgpu/WgpuSceneTextureBudgetTest.cpp)
	set_target_properties(WgpuSceneTextureBudgetTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(WgpuSceneTextureBudgetTest CoinWgpuExperimental Coin)
	target_include_directories(WgpuSceneTextureBudgetTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME WgpuSceneTextureBudgetTest COMMAND WgpuSceneTextureBudgetTest)
	set_tests_properties(WgpuSceneTextureBudgetTest PROPERTIES TIMEOUT 240)

	if(COIN_WGPU_BACKEND STREQUAL "RUST_BRIDGE")
		add_test(NAME WgpuSceneTextureDirectTest COMMAND WgpuSceneTextureTest)
		set_tests_properties(WgpuSceneTextureDirectTest PROPERTIES
			TIMEOUT 30 ENVIRONMENT "COIN_WGPU_RTT_GPU_DIRECT=1")
		add_test(NAME WgpuSceneTextureBudgetDirectTest COMMAND WgpuSceneTextureBudgetTest)
		set_tests_properties(WgpuSceneTextureBudgetDirectTest PROPERTIES
			TIMEOUT 240 ENVIRONMENT "COIN_WGPU_RTT_GPU_DIRECT=1")

		add_executable(WgpuFfiFrameTest
			wgpu/WgpuFfiFrameTest.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/wgpu/SoWgpuFfiFrame.cpp
		)
		set_target_properties(WgpuFfiFrameTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(WgpuFfiFrameTest Coin)
		target_include_directories(WgpuFfiFrameTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME WgpuFfiFrameTest COMMAND WgpuFfiFrameTest)
		set_tests_properties(WgpuFfiFrameTest PROPERTIES TIMEOUT 30)

		# Measurement utility only: no timing threshold is stable enough for CTest.
		add_executable(WgpuFrameReuseBenchmark
			wgpu/WgpuFrameReuseBenchmark.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/wgpu/SoWgpuFfiFrame.cpp
		)
		set_target_properties(WgpuFrameReuseBenchmark PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(WgpuFrameReuseBenchmark Coin)
		target_include_directories(WgpuFrameReuseBenchmark PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)

		add_executable(WgpuPerformanceTest wgpu/WgpuPerformanceTest.cpp)
		set_target_properties(WgpuPerformanceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(WgpuPerformanceTest CoinWgpuExperimental Coin)
		target_include_directories(WgpuPerformanceTest PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME WgpuPerformanceTest COMMAND WgpuPerformanceTest)
		set_tests_properties(WgpuPerformanceTest PROPERTIES TIMEOUT 30)

		add_executable(WgpuAsyncReadbackTest wgpu/WgpuAsyncReadbackTest.cpp)
		set_target_properties(WgpuAsyncReadbackTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(WgpuAsyncReadbackTest CoinWgpuExperimental Coin)
		target_include_directories(WgpuAsyncReadbackTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME WgpuAsyncReadbackTest COMMAND WgpuAsyncReadbackTest)
		set_tests_properties(WgpuAsyncReadbackTest PROPERTIES TIMEOUT 30)

		add_executable(WgpuMultiDeviceTest wgpu/WgpuMultiDeviceTest.cpp)
		set_target_properties(WgpuMultiDeviceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(WgpuMultiDeviceTest CoinWgpuExperimental Coin)
		target_include_directories(WgpuMultiDeviceTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME WgpuMultiDeviceTest COMMAND WgpuMultiDeviceTest)
		set_tests_properties(WgpuMultiDeviceTest PROPERTIES TIMEOUT 60)
		add_test(NAME WgpuMultiDeviceStressTest COMMAND WgpuMultiDeviceTest --stress)
		set_tests_properties(WgpuMultiDeviceStressTest PROPERTIES TIMEOUT 300)

		add_executable(WgpuAsyncActionTest wgpu/WgpuAsyncActionTest.cpp)
		set_target_properties(WgpuAsyncActionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(WgpuAsyncActionTest CoinWgpuExperimental Coin)
		target_include_directories(WgpuAsyncActionTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME WgpuAsyncActionTest COMMAND WgpuAsyncActionTest)
		set_tests_properties(WgpuAsyncActionTest PROPERTIES TIMEOUT 30)
		add_test(NAME WgpuAsyncActionDirectTest COMMAND WgpuAsyncActionTest)
		set_tests_properties(WgpuAsyncActionDirectTest PROPERTIES
			TIMEOUT 30 ENVIRONMENT "COIN_WGPU_RTT_GPU_DIRECT=1")
		add_test(NAME WgpuAsyncActionDirectStressTest COMMAND WgpuAsyncActionTest)
		set_tests_properties(WgpuAsyncActionDirectStressTest PROPERTIES
			TIMEOUT 240 ENVIRONMENT "COIN_WGPU_RTT_GPU_DIRECT=1;COIN_WGPU_RTT_STRESS=1")
	endif()

	if(COIN_TEST_WGPU_GL_REFERENCE)
		if(NOT (UNIX AND NOT APPLE AND X11_FOUND))
			message(FATAL_ERROR "COIN_TEST_WGPU_GL_REFERENCE requires X11 on Unix")
		endif()
		find_program(COIN_XVFB_RUN_EXECUTABLE xvfb-run)
		if(NOT COIN_XVFB_RUN_EXECUTABLE)
			message(FATAL_ERROR "COIN_TEST_WGPU_GL_REFERENCE requires xvfb-run")
		endif()
		if(COIN_WGPU_BACKEND STREQUAL "BGFX")
			foreach(renderer vulkan opengl)
				add_test(NAME BgfxReadbackModes_${renderer}_GlReference
					COMMAND ${CMAKE_COMMAND} -E env COIN_BGFX_RENDERER=${renderer} COIN_BGFX_TRANSPARENCY=auto
					COIN_BGFX_COMPARE_GL=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_GLXGLUE_NO_PBUFFERS=1
					${COIN_XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:BgfxReadbackModesTest>)
				set_tests_properties(BgfxReadbackModes_${renderer}_GlReference PROPERTIES TIMEOUT 90)
			endforeach()
		endif()
		add_test(NAME WgpuLightingGlReferenceTest
			COMMAND ${CMAKE_COMMAND} -E env
				LIBGL_ALWAYS_SOFTWARE=1
				COIN_GLX_PIXMAP_DIRECT_RENDERING=1
				COIN_GLXGLUE_NO_PBUFFERS=1
				COIN_WGPU_REQUIRE_GL_REFERENCE=1
				${COIN_XVFB_RUN_EXECUTABLE} -a
				-s "-screen 0 1024x768x24 +extension GLX +render -noreset"
				$<TARGET_FILE:WgpuLightingTest>)
		set_tests_properties(WgpuLightingGlReferenceTest PROPERTIES TIMEOUT 60)
		add_test(NAME WgpuFogGlReferenceTest
			COMMAND ${CMAKE_COMMAND} -E env
				LIBGL_ALWAYS_SOFTWARE=1
				COIN_GLX_PIXMAP_DIRECT_RENDERING=1
				COIN_GLXGLUE_NO_PBUFFERS=1
				COIN_WGPU_REQUIRE_GL_REFERENCE=1
				${COIN_XVFB_RUN_EXECUTABLE} -a
				-s "-screen 0 1024x768x24 +extension GLX +render -noreset"
				$<TARGET_FILE:WgpuFogTest>)
		set_tests_properties(WgpuFogGlReferenceTest PROPERTIES TIMEOUT 60)
	endif()

	if(COIN_WGPU_BACKEND STREQUAL "RUST_BRIDGE" AND (UNIX AND NOT APPLE AND X11_FOUND))
		add_executable(WgpuSurfaceTest
			wgpu/WgpuSurfaceTest.cpp
		)
		set_target_properties(WgpuSurfaceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H;HAVE_X11=1")
		target_link_libraries(WgpuSurfaceTest CoinWgpuExperimental Coin ${X11_LIBRARIES})
		target_include_directories(WgpuSurfaceTest PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}
			${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${X11_INCLUDE_DIR}
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME WgpuSurfaceTest COMMAND WgpuSurfaceTest)
		set_tests_properties(WgpuSurfaceTest PROPERTIES TIMEOUT 30)

		if(COIN_TEST_WGPU_STRICT_DISPLAY)
			add_test(NAME WgpuSurfaceDisplayStrictTest COMMAND WgpuSurfaceTest --require-display)
			set_tests_properties(WgpuSurfaceDisplayStrictTest PROPERTIES TIMEOUT 30)
		endif()
	endif()
endif()
