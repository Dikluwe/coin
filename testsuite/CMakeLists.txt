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
	target_sources(CoinTests PRIVATE GLUWrapperTest.cpp)
	target_include_directories(CoinTests PRIVATE ${PROJECT_BINARY_DIR}/src)
endif()
if (USE_PTHREAD)
	target_link_libraries(CoinTests pthread)
endif()
add_test(NAME CoinTests COMMAND CoinTests)

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

# Many warnings are generated from test macros on macOS with Xcode.
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Wtautological-compare DISABLE_WARNING_TAUTOLOGCOMPARE)
if(DISABLE_WARNING_TAUTOLOGCOMPARE)
	target_compile_options(CoinTests PUBLIC -Wno-tautological-compare)
endif()

option(COIN_TEST_WGPU_STRICT_DISPLAY "Enable strict X11 display test in CTest where missing DISPLAY is a failure" OFF)
option(COIN_TEST_WGPU_GL_REFERENCE "Enable strict Coin/GL offscreen lighting reference under Xvfb" OFF)

if(COIN_BUILD_WGPU)
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

	if(COIN_TEST_WGPU_GL_REFERENCE)
		if(NOT (UNIX AND NOT APPLE AND X11_FOUND))
			message(FATAL_ERROR "COIN_TEST_WGPU_GL_REFERENCE requires X11 on Unix")
		endif()
		find_program(COIN_XVFB_RUN_EXECUTABLE xvfb-run)
		if(NOT COIN_XVFB_RUN_EXECUTABLE)
			message(FATAL_ERROR "COIN_TEST_WGPU_GL_REFERENCE requires xvfb-run")
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
