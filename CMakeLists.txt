cmake_minimum_required(VERSION 3.31)
project(editing_text)

set(CMAKE_CXX_STANDARD 20)

add_executable(editing_text main.cpp)

find_package(Stb REQUIRED)
target_include_directories(editing_text PRIVATE ${Stb_INCLUDE_DIR})

find_package(SDL3_ttf CONFIG REQUIRED)
target_link_libraries(editing_text PRIVATE SDL3_ttf::SDL3_ttf)

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(editing_text PRIVATE SDL3::SDL3)

find_package(bgfx CONFIG REQUIRED)
target_link_libraries(editing_text PRIVATE bgfx::bx bgfx::bgfx bgfx::bimg bgfx::bimg_decode)