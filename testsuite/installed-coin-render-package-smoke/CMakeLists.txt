cmake_minimum_required(VERSION 3.15)
project(CoinRenderInstalledPackageSmoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Coin CONFIG REQUIRED)
find_package(CoinRender CONFIG REQUIRED)

add_executable(coin-render-installed-package-smoke main.cpp)
target_link_libraries(coin-render-installed-package-smoke PRIVATE
  CoinRender::CoinRender Coin::Coin)
