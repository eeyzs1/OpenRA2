cmake_minimum_required(VERSION 3.24)
project(OpenRA2 CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
)
FetchContent_MakeAvailable(raylib)

file(GLOB_RECURSE RA2_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_executable(ra2 ${RA2_SOURCES})
target_include_directories(ra2 PRIVATE src)
target_link_libraries(ra2 PRIVATE raylib)

if(MSVC)
    target_compile_options(ra2 PRIVATE /W3 /MP /utf-8)
endif()
