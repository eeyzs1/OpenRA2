cmake_minimum_required(VERSION 3.24)
project(OpenRA2 CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)

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
    target_compile_options(ra2 PRIVATE /W4 /MP /utf-8 /permissive- /Zc:__cplusplus)
    # GUI subsystem: Explorer/bat `start` closing the parent console must NOT
    # deliver CTRL_CLOSE_EVENT and kill the game (CUI + start = intermittent instant exit).
    target_link_options(ra2 PRIVATE /Brepro /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# 内存错误检测：cmake -B build-asan -DRA2_ASAN=ON（仅 MSVC Debug 有效）
option(RA2_ASAN "Enable AddressSanitizer" OFF)
if(MSVC AND RA2_ASAN)
    target_compile_options(ra2 PRIVATE /fsanitize=address /Zi)
    target_link_options(ra2 PRIVATE /INFERASANLIBS /INCREMENTAL:NO)
    target_compile_options(raylib PRIVATE /fsanitize=address)
endif()

# 无窗口纯逻辑基线：不链接 raylib，不初始化图形或音频设备。
if(BUILD_TESTING)
    add_executable(ra2_logic_tests
        tests/logic_tests.cpp
        src/game/map.cpp
    )
    target_include_directories(ra2_logic_tests PRIVATE src)
    if(MSVC)
        target_compile_options(ra2_logic_tests PRIVATE /W4 /MP /utf-8 /permissive- /Zc:__cplusplus)
        target_link_options(ra2_logic_tests PRIVATE /Brepro)
    endif()
    add_test(NAME ra2.logic COMMAND ra2_logic_tests)
    add_test(NAME ra2.state COMMAND ra2 --state-test)
    set_tests_properties(ra2.state PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 60
    )
    # 可失败的遭遇战 smoke / 32 关战役矩阵（期望不符返回非零）
    add_test(NAME ra2.smoke COMMAND ra2 --smoke)
    set_tests_properties(ra2.smoke PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 300
    )
    add_test(NAME ra2.campaign_matrix COMMAND ra2 --campaign-matrix)
    set_tests_properties(ra2.campaign_matrix PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 600
    )
    if(WIN32)
        add_test(
            NAME ra2.net_dual_process
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tests/net_dual_process.ps1
                    -Exe $<TARGET_FILE:ra2> -Frames 360
        )
        set_tests_properties(ra2.net_dual_process PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
            RUN_SERIAL TRUE
        )
        # Guard against BOM / LF-only / start /D "%~dp0" regressions that break Explorer double-click.
        add_test(
            NAME ra2.launch_bats
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_launch_bats.py
        )
        set_tests_properties(ra2.launch_bats PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 15
        )
    endif()
endif()
