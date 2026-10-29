# CPU environment payloads/baking; deliberately independent of CoinRender and graphics APIs.
add_library(CoinEnvironment SHARED CoinEnvironment.cpp CoinEnvironmentRadiance.cpp CoinEnvironmentExr.cpp CoinEnvironmentAnisotropy.cpp)
target_compile_definitions(CoinEnvironment PRIVATE COIN_ENVIRONMENT_INTERNAL)
target_compile_features(CoinEnvironment PUBLIC cxx_std_11)
target_include_directories(CoinEnvironment PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(CoinEnvironment PUBLIC Coin)
set_target_properties(CoinEnvironment PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(COIN_INSTALL_RENDER_EXPERIMENTAL)
  install(TARGETS CoinEnvironment EXPORT CoinRenderTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/Inventor/environment
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Inventor COMPONENT development FILES_MATCHING PATTERN "*.h")
  install(FILES ${PROJECT_SOURCE_DIR}/include/Inventor/CoinEnvironmentExport.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Inventor COMPONENT development)
endif()
add_executable(coin_render_ibl_baker ${PROJECT_SOURCE_DIR}/examples/coinrender/coin_render_ibl_baker.cpp)
target_link_libraries(coin_render_ibl_baker PRIVATE CoinEnvironment Coin)
