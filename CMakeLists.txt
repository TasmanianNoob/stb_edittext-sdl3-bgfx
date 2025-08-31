cmake_minimum_required(VERSION 3.31)
project(editing_text)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(CMAKE_CXX_STANDARD 20)

add_executable(editing_text main.cpp)

find_package(Stb REQUIRED)
target_include_directories(editing_text PRIVATE ${Stb_INCLUDE_DIR})

find_package(msdfgen CONFIG REQUIRED)
target_link_libraries(editing_text PRIVATE msdfgen::msdfgen msdfgen::msdfgen-ext msdfgen::msdfgen-core)

find_package(msdf-atlas-gen CONFIG REQUIRED)
target_link_libraries(editing_text PRIVATE msdf-atlas-gen::msdf-atlas-gen)

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(editing_text PRIVATE SDL3::SDL3)

find_package(bgfx CONFIG REQUIRED)
target_link_libraries(editing_text PRIVATE bgfx::bx bgfx::bgfx bgfx::bimg bgfx::bimg_decode)