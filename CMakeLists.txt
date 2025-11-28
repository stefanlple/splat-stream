cmake_minimum_required(VERSION 3.26)
project(SplatStream C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
add_subdirectory(third_party/3dgs_cpp EXCLUDE_FROM_ALL)

add_library(scene src/scene/ply_loader.cpp)
target_include_directories(scene PUBLIC src)
target_compile_options(scene PRIVATE -Wall -Wextra)

add_library(render src/render/splat_renderer.cpp)
target_link_libraries(render PUBLIC scene PRIVATE 3dgs_cpp)
target_compile_definitions(render PRIVATE VKGS_ENABLE_GLFW)
target_compile_options(render PRIVATE -Wall -Wextra)

add_executable(viewer apps/viewer/main.cpp)
target_link_libraries(viewer PRIVATE render)
target_compile_options(viewer PRIVATE -Wall -Wextra)

# glfw loads libvulkan at runtime by name, so the viewer needs an rpath to find it
get_filename_component(VULKAN_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
set_target_properties(viewer PROPERTIES BUILD_RPATH ${VULKAN_LIB_DIR})

find_package(Protobuf CONFIG REQUIRED)
find_path(ASIO_INCLUDE_DIR asio.hpp REQUIRED)
find_package(zstd CONFIG REQUIRED)

add_library(compression src/compression/zstd_codec.cpp)
target_include_directories(compression PUBLIC src)
target_link_libraries(compression PRIVATE zstd::libzstd)
target_compile_options(compression PRIVATE -Wall -Wextra)

add_library(protocol proto/messages.proto src/protocol/framing.cpp src/protocol/scene_stream.cpp)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
protobuf_generate(TARGET protocol IMPORT_DIRS proto PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(protocol PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(protocol PUBLIC protobuf::libprotobuf scene PRIVATE compression)

add_library(net src/net/server.cpp src/net/client.cpp)
target_include_directories(net PUBLIC ${ASIO_INCLUDE_DIR})
target_link_libraries(net PUBLIC protocol)
target_compile_options(net PRIVATE -Wall -Wextra)

add_executable(server apps/server/main.cpp)
target_link_libraries(server PRIVATE net scene)

add_executable(client apps/client/main.cpp)
target_link_libraries(client PRIVATE net render)
set_target_properties(client PROPERTIES BUILD_RPATH ${VULKAN_LIB_DIR})
