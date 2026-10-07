#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "CoinRender::CoinRender" for configuration "Release"
set_property(TARGET CoinRender::CoinRender APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(CoinRender::CoinRender PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "Coin::Coin"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libCoinRender.so"
  IMPORTED_SONAME_RELEASE "libCoinRender.so"
  )

list(APPEND _cmake_import_check_targets CoinRender::CoinRender )
list(APPEND _cmake_import_check_files_for_CoinRender::CoinRender "${_IMPORT_PREFIX}/lib/libCoinRender.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
