cmake_minimum_required(VERSION 3.24)
project(OpenRA2 CXX C)

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

# ---- Lua 5.4（用户脚本引擎：触发器/事件 hook/AI 定制）----
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.6.tar.gz
)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
    # Lua 核心库源文件（排除 lua.c/luac.c 可执行入口）
    set(LUA_CORE
        ${lua_SOURCE_DIR}/src/lapi.c ${lua_SOURCE_DIR}/src/lcode.c
        ${lua_SOURCE_DIR}/src/lctype.c ${lua_SOURCE_DIR}/src/ldebug.c
        ${lua_SOURCE_DIR}/src/ldo.c ${lua_SOURCE_DIR}/src/ldump.c
        ${lua_SOURCE_DIR}/src/lfunc.c ${lua_SOURCE_DIR}/src/lgc.c
        ${lua_SOURCE_DIR}/src/llex.c ${lua_SOURCE_DIR}/src/lmem.c
        ${lua_SOURCE_DIR}/src/lobject.c ${lua_SOURCE_DIR}/src/lopcodes.c
        ${lua_SOURCE_DIR}/src/lparser.c ${lua_SOURCE_DIR}/src/lstate.c
        ${lua_SOURCE_DIR}/src/lstring.c ${lua_SOURCE_DIR}/src/ltable.c
        ${lua_SOURCE_DIR}/src/ltm.c ${lua_SOURCE_DIR}/src/lundump.c
        ${lua_SOURCE_DIR}/src/lvm.c ${lua_SOURCE_DIR}/src/lzio.c
        ${lua_SOURCE_DIR}/src/lauxlib.c ${lua_SOURCE_DIR}/src/lbaselib.c
        ${lua_SOURCE_DIR}/src/lcorolib.c ${lua_SOURCE_DIR}/src/ldblib.c
        ${lua_SOURCE_DIR}/src/liolib.c ${lua_SOURCE_DIR}/src/lmathlib.c
        ${lua_SOURCE_DIR}/src/loadlib.c ${lua_SOURCE_DIR}/src/loslib.c
        ${lua_SOURCE_DIR}/src/lstrlib.c ${lua_SOURCE_DIR}/src/ltablib.c
        ${lua_SOURCE_DIR}/src/lutf8lib.c ${lua_SOURCE_DIR}/src/linit.c
    )
    add_library(lua_static STATIC ${LUA_CORE})
    target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(lua_static PRIVATE /wd4244 /wd4267 /wd4996)
    endif()
    FetchContent_MakeAvailable(lua)
endif()

file(GLOB_RECURSE RA2_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_executable(ra2 ${RA2_SOURCES})
target_include_directories(ra2 PRIVATE src)
target_link_libraries(ra2 PRIVATE raylib lua_static)

if(MSVC)
    target_compile_options(ra2 PRIVATE /W3 /MP /utf-8)
endif()

# 内存错误检测：cmake -B build-asan -DRA2_ASAN=ON（仅 MSVC Debug 有效）
option(RA2_ASAN "Enable AddressSanitizer" OFF)
if(MSVC AND RA2_ASAN)
    target_compile_options(ra2 PRIVATE /fsanitize=address /Zi)
    target_link_options(ra2 PRIVATE /INFERASANLIBS /INCREMENTAL:NO)
    target_compile_options(raylib PRIVATE /fsanitize=address)
endif()
