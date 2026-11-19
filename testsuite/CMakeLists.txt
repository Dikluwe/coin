include(cc-list/Tests.cmake)
include(glglue-lifetime/Tests.cmake)
include(action-apply-exceptions/Tests.cmake)
include(gl-render-exceptions/Tests.cmake)
# This regression test exercises limit handling without an OpenGL display.
add_executable(CoinPbufferLimitsTest PbufferLimitsTest.cpp)
target_include_directories(CoinPbufferLimitsTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME PbufferLimits COMMAND CoinPbufferLimitsTest)

include(SbSmallMapTests.cmake)
include(fonts/Tests.cmake)
