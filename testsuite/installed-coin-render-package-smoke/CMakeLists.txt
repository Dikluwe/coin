cmake_minimum_required(VERSION 3.15)
project(CoinRenderInstalledPackageSmoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Coin CONFIG REQUIRED)
find_package(CoinRender CONFIG REQUIRED)

add_executable(coin-render-installed-package-smoke main.cpp)
target_link_libraries(coin-render-installed-package-smoke PRIVATE
  CoinRender::CoinRender Coin::Coin)

# Legacy source/package entry points must forward to the renamed module.
find_package(CoinWgpuExperimental CONFIG REQUIRED)
add_executable(coin-render-legacy-source-smoke legacy.cpp)
target_link_libraries(coin-render-legacy-source-smoke PRIVATE
  CoinWgpuExperimental::CoinWgpuExperimental Coin::Coin)
