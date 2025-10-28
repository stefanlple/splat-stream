cmake_minimum_required(VERSION 3.20)
project(SplatStream CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(scene src/scene/ply_loader.cpp)
target_include_directories(scene PUBLIC src)
target_compile_options(scene PRIVATE -Wall -Wextra)
