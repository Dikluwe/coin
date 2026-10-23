cmake_minimum_required(VERSION 3.16)
project(B95VulkanStalePresent LANGUAGES CXX)
find_package(Vulkan REQUIRED)
find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
add_executable(B95VulkanStalePresent probe.cpp)
target_compile_features(B95VulkanStalePresent PRIVATE cxx_std_17)
target_include_directories(B95VulkanStalePresent PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../coinrender" ${X11_INCLUDE_DIR})
target_link_libraries(B95VulkanStalePresent PRIVATE
  Vulkan::Vulkan OpenGL::GL ${X11_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
# Manual external diagnostic only: deliberately no add_test or suite inclusion.
