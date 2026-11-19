include(cc-list/Tests.cmake)
include(glglue-lifetime/Tests.cmake)
include(action-apply-exceptions/Tests.cmake)
include(gl-render-exceptions/Tests.cmake)
include(SbSmallMapTests.cmake)

if(COIN_BUILD_LEGACY_GL_RENDERER)
  find_package(Threads REQUIRED)
  add_executable(CoinBumpProgramTest bumprender/FailureTest.cpp)
  target_compile_definitions(CoinBumpProgramTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
  target_include_directories(CoinBumpProgramTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/src
    ${PROJECT_SOURCE_DIR}/include/Inventor/annex ${COIN_TARGET_INCLUDE_DIRECTORIES})
  target_link_libraries(CoinBumpProgramTest Coin Threads::Threads ${COIN_TARGET_LINK_LIBRARIES})
  add_test(NAME BumpProgramFailuresAndLifetime COMMAND CoinBumpProgramTest)

  if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND AND HAVE_GLX)
      add_executable(CoinBumpGLXTest bumprender/GLXTest.cpp)
      target_compile_definitions(CoinBumpGLXTest PRIVATE COIN_INTERNAL HAVE_CONFIG_H)
      target_include_directories(CoinBumpGLXTest PRIVATE
        ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/src
        ${PROJECT_SOURCE_DIR}/include/Inventor/annex
        ${COIN_TARGET_INCLUDE_DIRECTORIES} ${X11_INCLUDE_DIR})
      target_link_libraries(CoinBumpGLXTest Coin ${COIN_TARGET_LINK_LIBRARIES} ${X11_LIBRARIES})
      add_test(NAME BumpProgramGLX COMMAND CoinBumpGLXTest)
      set_tests_properties(BumpProgramGLX PROPERTIES SKIP_RETURN_CODE 77)
    endif()
  endif()
endif()

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
		if("${FLSUBFLD}${FLNAME}" STREQUAL "basedict")
			set_source_files_properties("${FLSUBFLD}${FLNAME}Test.cpp" PROPERTIES
				COMPILE_DEFINITIONS COIN_INTERNAL)
		endif()
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
	InternalSmallMapTest.cpp
	${COIN_TEST_SOURCES}
)

# Private Coin symbols are not exported from the Windows DLL. Compile the
# dictionary and dynamic-array implementations, prime-number and SbHashFunc
# support directly into CoinTests. Static builds resolve these symbols from
# the Coin archive.
if(WIN32 AND COIN_BUILD_SHARED_LIBS)
	target_sources(CoinTests PRIVATE
		${PROJECT_SOURCE_DIR}/src/base/dict.cpp
		${PROJECT_SOURCE_DIR}/src/base/dynarray.cpp
		CoinGeqPrimeNumberWin32TestSupport.cpp
		SbHashFuncSbStringWin32TestSupport.cpp)
	set_source_files_properties(
		${PROJECT_SOURCE_DIR}/src/base/dict.cpp
		${PROJECT_SOURCE_DIR}/src/base/dynarray.cpp
		CoinGeqPrimeNumberWin32TestSupport.cpp
		SbHashFuncSbStringWin32TestSupport.cpp
		PROPERTIES
		COMPILE_DEFINITIONS COIN_INTERNAL)
endif()

set_source_files_properties(InternalMacroTest.cpp InternalSmallMapTest.cpp PROPERTIES
	COMPILE_DEFINITIONS COIN_INTERNAL)
set_property(SOURCE InternalSmallMapTest.cpp APPEND PROPERTY
	COMPILE_DEFINITIONS COIN_SMALLMAP_TESTING)
set_target_properties(CoinTests PROPERTIES DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}")
target_link_libraries(CoinTests Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(CoinTests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/include
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

if(HAVE_VRML97)
  add_executable(SoVRMLTextLayoutPublicTest
    SoVRMLTextLayoutPublicTest.cpp)
  target_link_libraries(SoVRMLTextLayoutPublicTest
    Coin ${COIN_TARGET_LINK_LIBRARIES})
  target_include_directories(SoVRMLTextLayoutPublicTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include
    ${COIN_TARGET_INCLUDE_DIRECTORIES})
  foreach(text_case
      directions minor-justify major-justify utf8-details
      length-maxextent-size spacing bbox-raypick-cache empty-strings)
    add_test(NAME SoVRMLText_${text_case}
      COMMAND SoVRMLTextLayoutPublicTest ${text_case})
    set_tests_properties(SoVRMLText_${text_case}
      PROPERTIES LABELS vrml97-primitives)
  endforeach()
endif()

