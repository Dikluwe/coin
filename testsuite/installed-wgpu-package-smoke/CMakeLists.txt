cmake_minimum_required(VERSION 3.15)
project(CoinWgpuInstalledPackageSmoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Coin CONFIG REQUIRED)
find_package(CoinWgpuExperimental CONFIG REQUIRED)

add_executable(coin-wgpu-installed-package-smoke main.cpp)
target_link_libraries(coin-wgpu-installed-package-smoke PRIVATE
  CoinWgpuExperimental::CoinWgpuExperimental Coin::Coin)
