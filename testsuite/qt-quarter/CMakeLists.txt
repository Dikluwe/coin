cmake_minimum_required(VERSION 3.20)
project(CoinQtIntegrationRegression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
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
find_library(COIN_LIBRARY NAMES Coin PATHS "${COIN_BUILD_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(WGPU_LIBRARY NAMES CoinWgpuExperimental PATHS "${COIN_BUILD_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(FREECAD_GUI_LIBRARY NAMES FreeCADGui PATHS "${FREECAD_BUILD_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
add_executable(QtQuarterRegression harness.cpp)
target_compile_definitions(QtQuarterRegression PRIVATE FREECAD_COIN_WGPU_EXPERIMENTAL)
target_include_directories(QtQuarterRegression PRIVATE
  "${COIN_SOURCE_DIR}/experimental/include" "${COIN_SOURCE_DIR}/include"
  "${COIN_BUILD_DIR}/include" "${COIN_BUILD_DIR}/src"
  "${FREECAD_SOURCE_DIR}/src" "${FREECAD_SOURCE_DIR}/src/Gui"
  "${FREECAD_BUILD_DIR}/src" "${X11_INCLUDE_DIR}")
target_link_libraries(QtQuarterRegression PRIVATE
  "${FREECAD_GUI_LIBRARY}" "${WGPU_LIBRARY}" "${COIN_LIBRARY}"
  Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test "${X11_LIBRARIES}")
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME QtQuarterResultGate COMMAND "${Python3_EXECUTABLE}" -m unittest discover
  -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_runner.py)
add_test(NAME QtQuarterMatrix COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/run.py" --harness "$<TARGET_FILE:QtQuarterRegression>"
  --artifacts "${CMAKE_BINARY_DIR}/artifacts")
set_tests_properties(QtQuarterMatrix PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77)
