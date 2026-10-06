cmake_minimum_required(VERSION 3.20)
project(CoinQtIntegrationRegression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(COIN_SOURCE_DIR "" CACHE PATH "Coin source tree matching the tested library")
set(COIN_BUILD_DIR "" CACHE PATH "Coin build tree matching the tested library")
set(FREECAD_SOURCE_DIR "" CACHE PATH "FreeCAD source tree matching FreeCADGui")
set(FREECAD_BUILD_DIR "" CACHE PATH "FreeCAD build with the WGPU Quarter integration")
foreach(path COIN_SOURCE_DIR COIN_BUILD_DIR FREECAD_SOURCE_DIR FREECAD_BUILD_DIR)
  if(NOT IS_DIRECTORY "${${path}}")
    message(FATAL_ERROR "Set ${path} to the existing integration tree")
  endif()
endforeach()
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Test)
find_package(X11 REQUIRED)
load_cache("${FREECAD_BUILD_DIR}" READ_WITH_PREFIX FREECAD_DEP_ _Python3_INCLUDE_DIR)
find_library(COIN_LIBRARY NAMES Coin PATHS "${COIN_BUILD_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(RENDER_LIBRARY NAMES CoinRender PATHS "${COIN_BUILD_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(PART_GUI_LIBRARY NAMES PartGui.so PartGui PATHS "${FREECAD_BUILD_DIR}/Mod/Part" NO_DEFAULT_PATH REQUIRED)
find_library(MESH_GUI_LIBRARY NAMES MeshGui.so MeshGui PATHS "${FREECAD_BUILD_DIR}/Mod/Mesh" NO_DEFAULT_PATH REQUIRED)
find_library(FREECAD_GUI_LIBRARY NAMES FreeCADGui PATHS "${FREECAD_BUILD_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
add_executable(QtQuarterRegression harness.cpp)
target_compile_definitions(QtQuarterRegression PRIVATE FREECAD_COIN_WGPU_EXPERIMENTAL)
target_include_directories(QtQuarterRegression PRIVATE
  "${COIN_SOURCE_DIR}/experimental/include" "${COIN_SOURCE_DIR}/include"
  "${COIN_BUILD_DIR}/include" "${COIN_BUILD_DIR}/src"
  "${FREECAD_SOURCE_DIR}/src" "${FREECAD_SOURCE_DIR}/src/Gui"
  "${FREECAD_BUILD_DIR}/src" "${X11_INCLUDE_DIR}")
target_include_directories(QtQuarterRegression PRIVATE
  "${FREECAD_SOURCE_DIR}/src/3rdParty/PyCXX" "${FREECAD_DEP__Python3_INCLUDE_DIR}"
  "${FREECAD_SOURCE_DIR}/src/3rdParty/FastSignals/libfastsignals/include"
  "${FREECAD_SOURCE_DIR}/src/3rdParty/WildMagic4" "${FREECAD_SOURCE_DIR}/src/3rdParty")
target_link_libraries(QtQuarterRegression PRIVATE
  "${FREECAD_GUI_LIBRARY}" "${PART_GUI_LIBRARY}" "${MESH_GUI_LIBRARY}" "${RENDER_LIBRARY}" "${COIN_LIBRARY}"
  Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test "${X11_LIBRARIES}")
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME QtQuarterResultGate COMMAND "${Python3_EXECUTABLE}" -m unittest discover
  -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_runner.py)
add_test(NAME QtQuarterMatrix COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/run.py" --harness "$<TARGET_FILE:QtQuarterRegression>"
  --artifacts "${CMAKE_BINARY_DIR}/artifacts")
set_tests_properties(QtQuarterMatrix PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77)

add_executable(FreeCADGeneratedImageTest FreeCADGeneratedImageTest.cpp)
target_include_directories(FreeCADGeneratedImageTest PRIVATE
  "${COIN_SOURCE_DIR}/include" "${COIN_BUILD_DIR}/include"
  "${FREECAD_SOURCE_DIR}/src/Gui")
target_link_libraries(FreeCADGeneratedImageTest PRIVATE Qt6::Gui ${COIN_LIBRARY})
add_test(NAME FreeCADGeneratedImageTest COMMAND FreeCADGeneratedImageTest)
