cmake_minimum_required(VERSION 3.20)
project(FreeCADOverlayTests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(FREECAD_SOURCE_DIR "" CACHE PATH "FreeCAD source checkout")
set(FREECAD_BUILD_DIR "" CACHE PATH "Experimental FreeCAD build")
set(COIN_PREFIX "" CACHE PATH "Matching experimental Coin installation")
option(FREECAD_OVERLAY_GPU_TESTS "Enable BGFX GPU readback tests" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_library(FREECAD_GUI FreeCADGui PATHS "${FREECAD_BUILD_DIR}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(FREECAD_BASE FreeCADBase PATHS "${FREECAD_BUILD_DIR}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(COIN_LIBRARY Coin PATHS "${COIN_PREFIX}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(COIN_GPU CoinRender PATHS "${COIN_PREFIX}/lib" REQUIRED NO_DEFAULT_PATH)

foreach(name IN ITEMS FreeCADLegacyOverlayTest FreeCADGLPainterTest FreeCADFlagRasterTest FreeCAD3DAnnotationTest)
    add_executable(${name} "../${name}.cpp")
    target_include_directories(${name} PRIVATE
        "${FREECAD_BUILD_DIR}" "${FREECAD_BUILD_DIR}/src"
        "${FREECAD_SOURCE_DIR}/src" "${COIN_PREFIX}/include")
    target_link_libraries(${name} PRIVATE
        ${FREECAD_GUI} ${FREECAD_BASE} ${COIN_GPU} ${COIN_LIBRARY}
        Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
find_package(Python3 REQUIRED COMPONENTS Development)
find_package(Eigen3 REQUIRED)
add_library(FreeCADFlagTestBridge SHARED ../FreeCADFlagTestBridge.cpp)
target_compile_definitions(FreeCADFlagTestBridge PRIVATE FREECAD_COIN_WGPU_EXPERIMENTAL QT_NO_KEYWORDS)
target_include_directories(FreeCADFlagTestBridge PRIVATE
    "${FREECAD_BUILD_DIR}" "${FREECAD_BUILD_DIR}/src"
    "${FREECAD_SOURCE_DIR}/src" "${COIN_PREFIX}/include")
target_include_directories(FreeCADFlagTestBridge PRIVATE
    "${FREECAD_SOURCE_DIR}/src/3rdParty/FastSignals/libfastsignals/include"
    "${FREECAD_SOURCE_DIR}/src/3rdParty/GSL/include"
    "${FREECAD_SOURCE_DIR}/src/3rdParty/PyCXX"
    "${FREECAD_SOURCE_DIR}/src/3rdParty")
target_link_libraries(FreeCADFlagTestBridge PRIVATE Python3::Python Eigen3::Eigen
    ${FREECAD_GUI} ${FREECAD_BASE} ${COIN_GPU} ${COIN_LIBRARY}
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
add_library(FreeCADDelayedOverlayTestBridge SHARED ../FreeCADDelayedOverlayTestBridge.cpp)
target_include_directories(FreeCADDelayedOverlayTestBridge PRIVATE
    "${FREECAD_SOURCE_DIR}/src/Gui" "${FREECAD_BUILD_DIR}/src/Gui")
target_compile_definitions(FreeCADDelayedOverlayTestBridge PRIVATE FREECAD_COIN_WGPU_EXPERIMENTAL QT_NO_KEYWORDS)
target_include_directories(FreeCADDelayedOverlayTestBridge PRIVATE
    "${FREECAD_BUILD_DIR}" "${FREECAD_BUILD_DIR}/src"
    "${FREECAD_SOURCE_DIR}/src" "${COIN_PREFIX}/include")
target_include_directories(FreeCADDelayedOverlayTestBridge PRIVATE
    "${FREECAD_SOURCE_DIR}/src/3rdParty/FastSignals/libfastsignals/include"
    "${FREECAD_SOURCE_DIR}/src/3rdParty/GSL/include"
    "${FREECAD_SOURCE_DIR}/src/3rdParty/PyCXX"
    "${FREECAD_SOURCE_DIR}/src/3rdParty")
target_link_libraries(FreeCADDelayedOverlayTestBridge PRIVATE Python3::Python Eigen3::Eigen
    ${FREECAD_GUI} ${FREECAD_BASE} ${COIN_GPU} ${COIN_LIBRARY}
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
set_tests_properties(FreeCADFlagRasterTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(FREECAD_OVERLAY_GPU_TESTS)
    foreach(renderer IN ITEMS vulkan opengl)
        foreach(mode IN ITEMS object weighted_oit)
            set(annotation "3DAnnotationGpu-${renderer}-${mode}")
            add_test(NAME ${annotation} COMMAND FreeCAD3DAnnotationTest --gpu)
            set_tests_properties(${annotation} PROPERTIES
                ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=${mode};COIN_RENDER_TRACE_PHASES=1"
                TIMEOUT 30 RUN_SERIAL TRUE)
            set(name "GLPainterGpu-${renderer}-${mode}")
            add_test(NAME ${name} COMMAND FreeCADGLPainterTest --gpu)
            set_tests_properties(${name} PROPERTIES
                ENVIRONMENT "COIN_BGFX_RENDERER=${renderer};COIN_BGFX_TRANSPARENCY=${mode};COIN_RENDER_TRACE_PHASES=1"
                TIMEOUT 30 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
endif()
