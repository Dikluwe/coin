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

option(COIN_TEST_RENDER_STRICT_DISPLAY "Enable strict X11 display test in CTest where missing DISPLAY is a failure" OFF)
option(COIN_TEST_RENDER_GL_REFERENCE "Enable strict Coin/GL offscreen lighting reference under Xvfb" OFF)

if(COIN_BUILD_RENDER)
	if((COIN_TEST_RENDER_STRICT_DISPLAY OR COIN_TEST_RENDER_GL_REFERENCE) AND
	   NOT COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
		message(FATAL_ERROR "Strict WebGPU display/GL reference tests require COIN_RENDER_BACKEND=RUST_BRIDGE")
	endif()
	add_executable(CoinRenderActionTest
		actions/CoinRenderActionTest.cpp
	)
	set_target_properties(CoinRenderActionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderActionTest CoinRender Coin)
	target_include_directories(CoinRenderActionTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderActionTest COMMAND CoinRenderActionTest)
	set_tests_properties(CoinRenderActionTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderSelectionTest coinrender/CoinRenderSelectionTest.cpp)
	set_target_properties(CoinRenderSelectionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderSelectionTest CoinRender Coin)
	target_include_directories(CoinRenderSelectionTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderSelectionTest COMMAND CoinRenderSelectionTest)
	set_tests_properties(CoinRenderSelectionTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer vulkan opengl)
			add_test(NAME CoinRenderSelection_${renderer} COMMAND CoinRenderSelectionTest)
			set_tests_properties(CoinRenderSelection_${renderer} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
		endforeach()
	endif()

	add_executable(CoinRenderProductTest coinrender/CoinRenderProductTest.cpp)
	set_target_properties(CoinRenderProductTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderProductTest CoinRender Coin)
	target_include_directories(CoinRenderProductTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
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
	add_executable(CoinRenderBackendContractTest coinrender/CoinRenderBackendContractTest.cpp)
	set_target_properties(CoinRenderBackendContractTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderBackendContractTest CoinRender Coin)
	target_include_directories(CoinRenderBackendContractTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderBackendContractTest COMMAND CoinRenderBackendContractTest)
	set_tests_properties(CoinRenderBackendContractTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

	add_executable(CoinRenderOffscreenTest
		coinrender/CoinRenderOffscreenTest.cpp
	)
	set_target_properties(CoinRenderOffscreenTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderOffscreenTest CoinRender Coin)
	target_include_directories(CoinRenderOffscreenTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderOffscreenTest COMMAND CoinRenderOffscreenTest)
	set_tests_properties(CoinRenderOffscreenTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderGateG0Test
		coinrender/CoinRenderGateG0Test.cpp
	)
	set_target_properties(CoinRenderGateG0Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderGateG0Test CoinRender Coin)
	target_include_directories(CoinRenderGateG0Test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderGateG0Test COMMAND CoinRenderGateG0Test)
	set_tests_properties(CoinRenderGateG0Test PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderFrameCoreTest coinrender/CoinRenderFrameCoreTest.cpp)
	set_target_properties(CoinRenderFrameCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderFrameCoreTest CoinRender Coin)
	target_include_directories(CoinRenderFrameCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderFrameCoreTest COMMAND CoinRenderFrameCoreTest)
	set_tests_properties(CoinRenderFrameCoreTest PROPERTIES TIMEOUT 30)

	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
	add_executable(CoinBgfxReadbackModesTest coinbgfx/CoinBgfxReadbackModesTest.cpp)
	set_target_properties(CoinBgfxReadbackModesTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxReadbackModesTest CoinRender Coin)
	target_include_directories(CoinBgfxReadbackModesTest PRIVATE ${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(renderer vulkan opengl)
		add_test(NAME CoinBgfxReadbackModes_${renderer} COMMAND ${CMAKE_COMMAND} -E env COIN_BGFX_RENDERER=${renderer} COIN_BGFX_TRANSPARENCY=auto $<TARGET_FILE:CoinBgfxReadbackModesTest>)
		set_tests_properties(CoinBgfxReadbackModes_${renderer} PROPERTIES TIMEOUT 90)
	endforeach()
	add_executable(CoinBgfxSurfaceFeaturesTest coinbgfx/CoinBgfxSurfaceFeaturesTest.cpp)
	set_target_properties(CoinBgfxSurfaceFeaturesTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxSurfaceFeaturesTest CoinRender Coin)
	target_include_directories(CoinBgfxSurfaceFeaturesTest PRIVATE ${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	foreach(renderer vulkan opengl)
		foreach(mode object weighted_oit sorted_layers)
			add_test(NAME CoinBgfxSurface_${renderer}_${mode} COMMAND ${CMAKE_COMMAND} -E env COIN_BGFX_RENDERER=${renderer} COIN_BGFX_TRANSPARENCY=${mode} $<TARGET_FILE:CoinBgfxSurfaceFeaturesTest>)
			set_tests_properties(CoinBgfxSurface_${renderer}_${mode} PROPERTIES TIMEOUT 90)
		endforeach()
	endforeach()
	endif()
	add_executable(CoinBgfxCoreTest coinbgfx/CoinBgfxCoreTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxLowering.cpp)
	set_target_properties(CoinBgfxCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinBgfxCoreTest CoinRender Coin)
	target_include_directories(CoinBgfxCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinBgfxCoreTest COMMAND CoinBgfxCoreTest)
	add_executable(CoinRenderDepthContractTest coinrender/CoinRenderDepthContractTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinbgfx/CoinBgfxLowering.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp)
	target_sources(CoinRenderDepthContractTest PRIVATE ${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp)
	set_target_properties(CoinRenderDepthContractTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderDepthContractTest CoinRender Coin)
	target_include_directories(CoinRenderDepthContractTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src)
	add_test(NAME CoinRenderDepthContractTest COMMAND CoinRenderDepthContractTest)
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
		add_executable(CoinBgfxOffscreenTest coinbgfx/CoinBgfxOffscreenTest.cpp)
		set_target_properties(CoinBgfxOffscreenTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinBgfxOffscreenTest CoinRender Coin)
		target_include_directories(CoinBgfxOffscreenTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
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
		add_test(NAME CoinRenderBackendContractOpenGLTest COMMAND CoinRenderBackendContractTest)
		set_tests_properties(CoinRenderBackendContractOpenGLTest PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
			ENVIRONMENT "COIN_BGFX_RENDERER=opengl")
		add_executable(CoinBgfxTransparencyTest coinbgfx/CoinBgfxTransparencyTest.cpp)
		set_target_properties(CoinBgfxTransparencyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinBgfxTransparencyTest CoinRender Coin)
		target_include_directories(CoinBgfxTransparencyTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
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
			add_executable(CoinBgfxWindowTest coinbgfx/CoinBgfxWindowTest.cpp)
			set_target_properties(CoinBgfxWindowTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
			target_link_libraries(CoinBgfxWindowTest CoinRender Coin ${X11_LIBRARIES})
			target_include_directories(CoinBgfxWindowTest PRIVATE
				${PROJECT_SOURCE_DIR}/experimental/include
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
			add_executable(CoinBgfxMultiWindowTest coinbgfx/CoinBgfxMultiWindowTest.cpp)
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
				foreach(_renderer vulkan opengl)
					set(_multi_composition_test "CoinBgfxMultiWindow_${_composition}_${_renderer}Test")
					add_test(NAME ${_multi_composition_test} COMMAND CoinBgfxMultiWindowTest)
					set_tests_properties(${_multi_composition_test} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
						ENVIRONMENT "COIN_BGFX_TRANSPARENCY=${_composition};COIN_BGFX_RENDERER=${_renderer}")
				endforeach()
			endforeach()
		endif()
	endif()

	add_executable(CoinRenderFrameReuseCoreTest coinrender/CoinRenderFrameReuseCoreTest.cpp)
	set_target_properties(CoinRenderFrameReuseCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderFrameReuseCoreTest CoinRender Coin)
	target_include_directories(CoinRenderFrameReuseCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderFrameReuseCoreTest COMMAND CoinRenderFrameReuseCoreTest)
	set_tests_properties(CoinRenderFrameReuseCoreTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderDiagnosticShellTest coinrender/CoinRenderDiagnosticShellTest.cpp)
	set_target_properties(CoinRenderDiagnosticShellTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderDiagnosticShellTest CoinRender Coin)
	target_include_directories(CoinRenderDiagnosticShellTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderDiagnosticShellTest COMMAND CoinRenderDiagnosticShellTest)
	set_tests_properties(CoinRenderDiagnosticShellTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderIndexedGeometryCoreTest coinrender/CoinRenderIndexedGeometryCoreTest.cpp)
	set_target_properties(CoinRenderIndexedGeometryCoreTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderIndexedGeometryCoreTest CoinRender Coin)
	target_include_directories(CoinRenderIndexedGeometryCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderIndexedGeometryCoreTest COMMAND CoinRenderIndexedGeometryCoreTest)
	set_tests_properties(CoinRenderIndexedGeometryCoreTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderGateG1Test
		coinrender/CoinRenderGateG1Test.cpp
	)
	set_target_properties(CoinRenderGateG1Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderGateG1Test CoinRender Coin)
	target_include_directories(CoinRenderGateG1Test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderGateG1Test COMMAND CoinRenderGateG1Test)
	set_tests_properties(CoinRenderGateG1Test PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderGateG2Test
		coinrender/CoinRenderGateG2Test.cpp
	)
	set_target_properties(CoinRenderGateG2Test PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderGateG2Test CoinRender Coin)
	target_include_directories(CoinRenderGateG2Test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderGateG2Test COMMAND CoinRenderGateG2Test)
	set_tests_properties(CoinRenderGateG2Test PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderIndexedFastPathTest
		coinrender/CoinRenderIndexedFastPathTest.cpp
	)
	set_target_properties(CoinRenderIndexedFastPathTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderIndexedFastPathTest CoinRender Coin)
	target_include_directories(CoinRenderIndexedFastPathTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderIndexedFastPathTest COMMAND CoinRenderIndexedFastPathTest)
	set_tests_properties(CoinRenderIndexedFastPathTest PROPERTIES TIMEOUT 30)

	add_executable(CoinWgpuCacheTest
		coinwgpu/CoinWgpuCacheTest.cpp
	)
	set_target_properties(CoinWgpuCacheTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinWgpuCacheTest CoinRender Coin)
	target_include_directories(CoinWgpuCacheTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinWgpuCacheTest COMMAND CoinWgpuCacheTest)
	set_tests_properties(CoinWgpuCacheTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderStabilizationTest
		coinrender/CoinRenderStabilizationTest.cpp
	)
	set_target_properties(CoinRenderStabilizationTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderStabilizationTest CoinRender Coin)
	target_include_directories(CoinRenderStabilizationTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderStabilizationTest COMMAND CoinRenderStabilizationTest)
	set_tests_properties(CoinRenderStabilizationTest PROPERTIES TIMEOUT 30)

	if(COIN_RENDER_BACKEND STREQUAL "RUST_BRIDGE")
	add_executable(CoinRenderMaterialTest
		coinrender/CoinRenderMaterialTest.cpp
	)
	set_target_properties(CoinRenderMaterialTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderMaterialTest CoinRender Coin)
	target_include_directories(CoinRenderMaterialTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
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
		coinrender/CoinRenderTextureTest.cpp
	)
	set_target_properties(CoinRenderTextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderTextureTest CoinRender Coin)
	target_include_directories(CoinRenderTextureTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderTextureTest COMMAND CoinRenderTextureTest)
	set_tests_properties(CoinRenderTextureTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderLightingTest
		coinrender/CoinRenderLightingTest.cpp
	)
	set_target_properties(CoinRenderLightingTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderLightingTest CoinRender Coin)
	target_include_directories(CoinRenderLightingTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderLightingTest COMMAND CoinRenderLightingTest)
	set_tests_properties(CoinRenderLightingTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderGouraudInterpolationTest
		coinrender/CoinRenderGouraudInterpolationTest.cpp
	)
	set_target_properties(CoinRenderGouraudInterpolationTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderGouraudInterpolationTest CoinRender Coin)
	target_include_directories(CoinRenderGouraudInterpolationTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderGouraudInterpolationTest COMMAND CoinRenderGouraudInterpolationTest)
	set_tests_properties(CoinRenderGouraudInterpolationTest PROPERTIES TIMEOUT 30)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(_gouraud_renderer vulkan opengl)
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
		coinrender/CoinRenderFogTest.cpp
	)
	set_target_properties(CoinRenderFogTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderFogTest CoinRender Coin)
	target_include_directories(CoinRenderFogTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderFogTest COMMAND CoinRenderFogTest)
	set_tests_properties(CoinRenderFogTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderClipPlaneTest coinrender/CoinRenderClipPlaneTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp)
	set_target_properties(CoinRenderClipPlaneTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderClipPlaneTest CoinRender Coin)
	target_include_directories(CoinRenderClipPlaneTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
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
		foreach(renderer vulkan opengl)
			foreach(mode object weighted_oit sorted_layers)
				add_test(NAME CoinRenderClipPlane_${renderer}_${mode} COMMAND CoinRenderClipPlaneTest)
				set_tests_properties(CoinRenderClipPlane_${renderer}_${mode} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
					ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=${mode}")
			endforeach()
		endforeach()
	endif()


	add_executable(CoinRenderDrawStyleTest coinrender/CoinRenderDrawStyleTest.cpp
		${PROJECT_SOURCE_DIR}/src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp)
	set_target_properties(CoinRenderDrawStyleTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderDrawStyleTest CoinRender Coin)
	target_include_directories(CoinRenderDrawStyleTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderDrawStyleTest COMMAND CoinRenderDrawStyleTest)
	set_tests_properties(CoinRenderDrawStyleTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		set_tests_properties(CoinRenderDrawStyleTest PROPERTIES ENVIRONMENT "COIN_BGFX_RENDERER=vulkan;COIN_BGFX_TRANSPARENCY=object")
		foreach(renderer vulkan opengl)
			add_test(NAME CoinRenderDrawStyle_${renderer} COMMAND CoinRenderDrawStyleTest)
			set_tests_properties(CoinRenderDrawStyle_${renderer} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
		endforeach()
	endif()


	add_executable(CoinRenderMultitextureTest coinrender/CoinRenderMultitextureTest.cpp)
	set_target_properties(CoinRenderMultitextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderMultitextureTest CoinRender Coin)
	target_include_directories(CoinRenderMultitextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderMultitextureTest COMMAND CoinRenderMultitextureTest)
	set_tests_properties(CoinRenderMultitextureTest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer vulkan opengl)
			add_test(NAME CoinRenderMultitexture_${renderer} COMMAND CoinRenderMultitextureTest)
			set_tests_properties(CoinRenderMultitexture_${renderer} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=object")
		endforeach()
	endif()

	add_executable(CoinRenderPeelingTest coinrender/CoinRenderPeelingTest.cpp)
	set_target_properties(CoinRenderPeelingTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderPeelingTest CoinRender Coin)
	target_include_directories(CoinRenderPeelingTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderPeelingTest COMMAND CoinRenderPeelingTest)
	set_tests_properties(CoinRenderPeelingTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer vulkan opengl)
			add_test(NAME CoinRenderPeeling_${renderer} COMMAND CoinRenderPeelingTest)
			add_test(NAME CoinRenderOit_${renderer} COMMAND CoinRenderPeelingTest --weighted)
			set_tests_properties(CoinRenderOit_${renderer} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=weighted_oit")
			set_tests_properties(CoinRenderPeeling_${renderer} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
		endforeach()
	endif()

	add_executable(CoinRenderTransparencyTest coinrender/CoinRenderTransparencyTest.cpp)
	set_target_properties(CoinRenderTransparencyTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderTransparencyTest CoinRender Coin)
	target_include_directories(CoinRenderTransparencyTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderTransparencyTest COMMAND CoinRenderTransparencyTest)
	set_tests_properties(CoinRenderTransparencyTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer vulkan opengl)
			add_test(NAME CoinRenderTransparency_${renderer} COMMAND CoinRenderTransparencyTest)
			set_tests_properties(CoinRenderTransparency_${renderer} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
		endforeach()
	endif()

	add_executable(CoinRenderCompositionTest coinrender/CoinRenderCompositionTest.cpp)
	set_target_properties(CoinRenderCompositionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderCompositionTest CoinRender Coin)
	target_include_directories(CoinRenderCompositionTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/experimental/include
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
	endif()

	if(NOT CMAKE_VERSION VERSION_LESS "3.12")
		find_package(Python3 3.7 COMPONENTS Interpreter QUIET)
		if(Python3_Interpreter_FOUND)
			add_test(NAME CoinRenderNodeInventoryDiscoveryTest
				COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/testsuite/coinrender/test_node_inventory.py)
			set_tests_properties(CoinRenderNodeInventoryDiscoveryTest PROPERTIES TIMEOUT 30)
		endif()
	endif()

	add_executable(CoinRenderNodeInventoryTest coinrender/CoinRenderNodeInventoryTest.cpp)
	set_target_properties(CoinRenderNodeInventoryTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderNodeInventoryTest CoinRender Coin)
	target_include_directories(CoinRenderNodeInventoryTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderNodeInventoryTest COMMAND CoinRenderNodeInventoryTest)
	set_tests_properties(CoinRenderNodeInventoryTest PROPERTIES TIMEOUT 30)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		add_executable(CoinRenderShadowReferenceTest coinrender/CoinRenderShadowReferenceTest.cpp)
		set_target_properties(CoinRenderShadowReferenceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderShadowReferenceTest CoinRender Coin)
		target_include_directories(CoinRenderShadowReferenceTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderShadowReferenceTest COMMAND CoinRenderShadowReferenceTest)
		set_tests_properties(CoinRenderShadowReferenceTest PROPERTIES TIMEOUT 60)
	endif()

	add_executable(CoinRenderMultiTargetTest coinrender/CoinRenderMultiTargetTest.cpp)
	set_target_properties(CoinRenderMultiTargetTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderMultiTargetTest CoinRender Coin)
	target_include_directories(CoinRenderMultiTargetTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
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

	add_executable(CoinRenderPublicationTest coinrender/CoinRenderPublicationTest.cpp)
	set_target_properties(CoinRenderPublicationTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderPublicationTest CoinRender Coin)
	target_include_directories(CoinRenderPublicationTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderPublicationTest COMMAND CoinRenderPublicationTest)
	set_tests_properties(CoinRenderPublicationTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderRttOwnershipTest coinrender/CoinRenderRttOwnershipTest.cpp)
	set_target_properties(CoinRenderRttOwnershipTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderRttOwnershipTest CoinRender Coin)
	target_include_directories(CoinRenderRttOwnershipTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/src ${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME CoinRenderRttOwnershipTest COMMAND CoinRenderRttOwnershipTest)
	set_tests_properties(CoinRenderRttOwnershipTest PROPERTIES TIMEOUT 30)
	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer vulkan opengl)
			add_test(NAME CoinRenderRttOwnership_${renderer} COMMAND CoinRenderRttOwnershipTest)
			set_tests_properties(CoinRenderRttOwnership_${renderer} PROPERTIES TIMEOUT 30
				ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=auto")
		endforeach()
	endif()


	add_executable(CoinRenderSceneTextureTest coinrender/CoinRenderSceneTextureTest.cpp)
	set_target_properties(CoinRenderSceneTextureTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderSceneTextureTest CoinRender Coin)
	target_include_directories(CoinRenderSceneTextureTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderSceneTextureTest COMMAND CoinRenderSceneTextureTest)
	set_tests_properties(CoinRenderSceneTextureTest PROPERTIES TIMEOUT 30)

	add_executable(CoinRenderSceneTextureBudgetTest coinrender/CoinRenderSceneTextureBudgetTest.cpp)
	set_target_properties(CoinRenderSceneTextureBudgetTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
	target_link_libraries(CoinRenderSceneTextureBudgetTest CoinRender Coin)
	target_include_directories(CoinRenderSceneTextureBudgetTest PRIVATE
		${PROJECT_SOURCE_DIR}/experimental/include
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/include
		${PROJECT_BINARY_DIR}/src
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME CoinRenderSceneTextureBudgetTest COMMAND CoinRenderSceneTextureBudgetTest)
	set_tests_properties(CoinRenderSceneTextureBudgetTest PROPERTIES TIMEOUT 240)

	if(COIN_RENDER_BACKEND STREQUAL "BGFX")
		foreach(renderer vulkan opengl)
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
		add_test(NAME CoinRenderSceneTextureBudgetDirectTest COMMAND CoinRenderSceneTextureBudgetTest)
		set_tests_properties(CoinRenderSceneTextureBudgetDirectTest PROPERTIES
			TIMEOUT 240 ENVIRONMENT "COIN_RENDER_RTT_GPU_DIRECT=1")

		add_executable(CoinWgpuFfiFrameTest
			coinwgpu/CoinWgpuFfiFrameTest.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
		)
		set_target_properties(CoinWgpuFfiFrameTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinWgpuFfiFrameTest Coin)
		target_include_directories(CoinWgpuFfiFrameTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinWgpuFfiFrameTest COMMAND CoinWgpuFfiFrameTest)
		set_tests_properties(CoinWgpuFfiFrameTest PROPERTIES TIMEOUT 30)

		# Measurement utility only: no timing threshold is stable enough for CTest.
		add_executable(CoinRenderFrameReuseBenchmark
			coinrender/CoinRenderFrameReuseBenchmark.cpp
			${PROJECT_SOURCE_DIR}/src/rendering/coinwgpu/CoinWgpuFfiFrame.cpp
		)
		set_target_properties(CoinRenderFrameReuseBenchmark PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderFrameReuseBenchmark Coin)
		target_include_directories(CoinRenderFrameReuseBenchmark PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)

		add_executable(CoinRenderPerformanceTest coinrender/CoinRenderPerformanceTest.cpp)
		set_target_properties(CoinRenderPerformanceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderPerformanceTest CoinRender Coin)
		target_include_directories(CoinRenderPerformanceTest PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinRenderPerformanceTest COMMAND CoinRenderPerformanceTest)
		set_tests_properties(CoinRenderPerformanceTest PROPERTIES TIMEOUT 30)

		add_executable(CoinRenderAsyncReadbackTest coinrender/CoinRenderAsyncReadbackTest.cpp)
		set_target_properties(CoinRenderAsyncReadbackTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderAsyncReadbackTest CoinRender Coin)
		target_include_directories(CoinRenderAsyncReadbackTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinRenderAsyncReadbackTest COMMAND CoinRenderAsyncReadbackTest)
		set_tests_properties(CoinRenderAsyncReadbackTest PROPERTIES TIMEOUT 30)

		add_executable(CoinWgpuMultiDeviceTest coinwgpu/CoinWgpuMultiDeviceTest.cpp)
		set_target_properties(CoinWgpuMultiDeviceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinWgpuMultiDeviceTest CoinRender Coin)
		target_include_directories(CoinWgpuMultiDeviceTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_BINARY_DIR}/include
			${PROJECT_BINARY_DIR}/src
			${COIN_TARGET_INCLUDE_DIRECTORIES}
		)
		add_test(NAME CoinWgpuMultiDeviceTest COMMAND CoinWgpuMultiDeviceTest)
		set_tests_properties(CoinWgpuMultiDeviceTest PROPERTIES TIMEOUT 60)
		add_test(NAME WgpuMultiDeviceStressTest COMMAND CoinWgpuMultiDeviceTest --stress)
		set_tests_properties(WgpuMultiDeviceStressTest PROPERTIES TIMEOUT 300)

		add_executable(CoinRenderAsyncActionTest coinrender/CoinRenderAsyncActionTest.cpp)
		set_target_properties(CoinRenderAsyncActionTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H")
		target_link_libraries(CoinRenderAsyncActionTest CoinRender Coin)
		target_include_directories(CoinRenderAsyncActionTest PRIVATE
			${PROJECT_SOURCE_DIR}/experimental/include
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
			foreach(renderer vulkan opengl)
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
			coinrender/CoinRenderSurfaceTest.cpp
		)
		set_target_properties(CoinRenderSurfaceTest PROPERTIES COMPILE_DEFINITIONS "COIN_INTERNAL;HAVE_CONFIG_H;HAVE_X11=1")
		target_link_libraries(CoinRenderSurfaceTest CoinRender Coin ${X11_LIBRARIES})
		target_include_directories(CoinRenderSurfaceTest PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}
			${PROJECT_SOURCE_DIR}/experimental/include
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
		endif()
	endif()
endif()
