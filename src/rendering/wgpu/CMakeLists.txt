# CMakeLists.txt for CoinWgpuExperimental shared library

set(COIN_WGPU_SOURCES
	SoWgpuFramePlanBuilder.cpp
	SoWgpuRecordingBackend.cpp
	SoWgpuCpuReferenceBackend.cpp
	SoWgpuRenderTarget.cpp
	${PROJECT_SOURCE_DIR}/src/actions/SoWgpuRenderAction.cpp
)

if(COIN_WGPU_BACKEND STREQUAL "RUST_BRIDGE")
	list(APPEND COIN_WGPU_SOURCES SoWgpuRustBackend.cpp)
elseif(COIN_WGPU_BACKEND STREQUAL "DAWN" OR COIN_WGPU_BACKEND STREQUAL "WGPU_NATIVE")
	list(APPEND COIN_WGPU_SOURCES SoWgpuNativeBackend.cpp)
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
)

target_include_directories(CoinWgpuExperimental PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_BINARY_DIR}/src
)

target_link_libraries(CoinWgpuExperimental PRIVATE
	Coin
	${COIN_WGPU_LINK_LIBRARIES}
)
