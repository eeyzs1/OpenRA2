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

# 内存错误检测：cmake -B build-asan -DRA2_ASAN=ON（MSVC Debug/Release）
option(RA2_ASAN "Enable AddressSanitizer" OFF)
if(MSVC AND RA2_ASAN)
    target_compile_options(ra2 PRIVATE /fsanitize=address /Zi)
    target_compile_definitions(ra2 PRIVATE RA2_ASAN_ACTIVE=1)
    target_link_options(ra2 PRIVATE /INFERASANLIBS /INCREMENTAL:NO /DEBUG)
    target_compile_options(raylib PRIVATE /fsanitize=address)
endif()

# 无窗口纯逻辑基线：不链接 raylib，不初始化图形或音频设备。
if(BUILD_TESTING)
    target_sources(ra2 PRIVATE tests/legacy_v18_writer.cpp tests/legacy_v19_writer.cpp tests/legacy_v20_writer.cpp tests/legacy_v21_writer.cpp tests/legacy_v22_writer.cpp)
    target_compile_definitions(ra2 PRIVATE RA2_TESTING)
    add_executable(ra2_logic_tests
        tests/logic_tests.cpp
        src/game/map.cpp
        src/game/map_startup.cpp
    )
    target_include_directories(ra2_logic_tests PRIVATE src)
    if(MSVC)
        target_compile_options(ra2_logic_tests PRIVATE /W4 /MP /utf-8 /permissive- /Zc:__cplusplus)
        target_link_options(ra2_logic_tests PRIVATE /Brepro)
    endif()
    add_test(NAME ra2.input COMMAND ra2 --play-test)
    set_tests_properties(ra2.input PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "interaction;scripted")
    add_test(NAME ra2.economy COMMAND ra2 --economy-regression)
    set_tests_properties(ra2.economy PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;recovery")
    add_test(NAME ra2.movement COMMAND ra2 --movement-regression)
    foreach(combat_case RANGE 0 3)
        add_test(NAME ra2.combat_pressure_${combat_case} COMMAND ra2 --combat-pressure-regression ${combat_case} ${CMAKE_BINARY_DIR}/combat-pressure-audit)
        set_tests_properties(ra2.combat_pressure_${combat_case} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;combat;performance;determinism;persistence")
        if(combat_case GREATER 1)
            set_property(TEST ra2.combat_pressure_${combat_case} APPEND PROPERTY LABELS "presentation;screenshot")
        endif()
    endforeach()
    add_test(NAME ra2.traffic COMMAND ra2 --traffic-regression)
    # Keep instrumented finite traffic cases independent: combining all four
    # spent the aggregate timeout before the last case could finish.
    if(MSVC AND RA2_ASAN)
        foreach(junction_case RANGE 0 7)
            add_test(NAME ra2.junction_${junction_case} COMMAND ra2 --junction-regression ${junction_case})
            set_tests_properties(ra2.junction_${junction_case} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 LABELS "mechanism;movement;recovery;determinism;persistence")
        endforeach()
    else()
        add_test(NAME ra2.junction COMMAND ra2 --junction-regression)
        set_tests_properties(ra2.junction PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;movement;recovery;determinism;persistence")
        foreach(junction_case RANGE 4 7)
            add_test(NAME ra2.junction_${junction_case} COMMAND ra2 --junction-regression ${junction_case})
            set_tests_properties(ra2.junction_${junction_case} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;movement;recovery;determinism;persistence")
        endforeach()
    endif()
    add_test(NAME ra2.blockers COMMAND ra2 --blocker-regression)
    add_test(NAME ra2.idle_yield COMMAND ra2 --idle-yield-regression)
    add_test(NAME ra2.chopper_ground COMMAND ra2 --chopper-ground-regression)
    add_test(NAME ra2.immobilized COMMAND ra2 --immobilized-regression)
    add_test(NAME ra2.deployment_feedback COMMAND ra2 --deployment-feedback-regression)
    add_test(NAME ra2.rally COMMAND ra2 --rally-regression)
    add_test(NAME ra2.rally_visual COMMAND ra2 --rally-visual-regression ${CMAKE_BINARY_DIR}/rally-visual-audit)
    set_tests_properties(ra2.rally_visual PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "presentation;interaction;screenshot;determinism" RUN_SERIAL TRUE)
    add_test(NAME ra2.refinery_recovery COMMAND ra2 --refinery-recovery-regression)
    set_tests_properties(ra2.refinery_recovery PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;recovery;economy")
    set_tests_properties(ra2.rally PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence;recovery")
    add_test(NAME ra2.feedback_visual COMMAND ra2 --feedback-visual-regression ${CMAKE_BINARY_DIR}/feedback-visual-audit)
    set_tests_properties(ra2.feedback_visual PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "presentation;interaction;screenshot;determinism" RUN_SERIAL TRUE)
    set_tests_properties(ra2.deployment_feedback PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;presentation;determinism")
    add_test(NAME ra2.magnetic_landing COMMAND ra2 --magnetic-landing-regression ${CMAKE_BINARY_DIR}/magnetic-landing-audit)
    set_tests_properties(ra2.magnetic_landing PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;interaction")
    set_tests_properties(ra2.immobilized PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;interaction;recovery")
    set_tests_properties(ra2.chopper_ground PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;interaction")
    set_tests_properties(ra2.idle_yield PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;determinism;persistence;recovery")
    set_tests_properties(ra2.blockers PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;recovery")
    add_test(NAME ra2.ai_attack COMMAND ra2 --ai-attack-regression)
    add_test(NAME ra2.magnetron COMMAND ra2 --magnetron-regression ${CMAKE_BINARY_DIR}/magnetron-audit)
    set_tests_properties(ra2.magnetron PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "mechanism;combat;determinism;persistence;presentation;screenshot")
    set_tests_properties(ra2.ai_attack PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "mechanism;ai;determinism")
    add_test(NAME ra2.ai_economy COMMAND ra2 --ai-economy-regression)
    set_tests_properties(ra2.ai_economy PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;ai;economy;recovery;determinism;persistence")
    add_test(NAME ra2.ai_air_defense COMMAND ra2 --ai-air-defense-regression)
    set_tests_properties(ra2.ai_air_defense PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;ai;interaction;movement;recovery;determinism;persistence")
    add_test(NAME ra2.ai_factory_exit COMMAND ra2 --ai-factory-exit-regression)
    set_tests_properties(ra2.ai_factory_exit PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;ai;production;recovery;movement;determinism;persistence")
    add_test(NAME ra2.ai_transport COMMAND ra2 --ai-transport-regression)
    set_tests_properties(ra2.ai_transport PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;ai;movement;interaction;determinism;persistence")
    add_test(NAME ra2.moving_yield COMMAND ra2 --moving-yield-regression)
    set_tests_properties(ra2.moving_yield PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;movement;recovery;determinism;persistence")
    add_test(NAME ra2.generated_match_recovery COMMAND ra2 --generated-match 36000 2 305419896 3)
    set_tests_properties(ra2.generated_match_recovery PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 LABELS "natural-start;full-match;ai;economy;movement;recovery;determinism;persistence")
    add_test(NAME ra2.match_completion COMMAND ra2 --skirmish-regression 72000 -1 0 --require-outcomes)
    set_tests_properties(ra2.match_completion PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 LABELS "natural-start;full-match;determinism;persistence")
    set_tests_properties(ra2.traffic PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;determinism;recovery;persistence")
    if(MSVC AND RA2_ASAN)
        # Keep the complete traffic/replay checks under ASan instrumentation.
        # This only extends process time; movement and tick budgets stay fixed.
        set_tests_properties(ra2.traffic PROPERTIES TIMEOUT 600)
    endif()
    add_test(NAME ra2.attack COMMAND ra2 --attack-regression)
    add_test(NAME ra2.apocalypse COMMAND ra2 --apocalypse-regression)
    add_test(NAME ra2.burst COMMAND ra2 --burst-regression)
    add_test(NAME ra2.splash COMMAND ra2 --splash-regression)
    set_tests_properties(ra2.splash PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;combat;determinism;persistence")
    set_tests_properties(ra2.burst PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;combat;determinism;persistence")
    set_tests_properties(ra2.apocalypse PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;combat;determinism;persistence")
    set_tests_properties(ra2.attack PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism")
    add_test(NAME ra2.engineer COMMAND ra2 --engineer-regression)
    set_tests_properties(ra2.engineer PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence")
    add_test(NAME ra2.lifecycle COMMAND ra2 --lifecycle-regression)
    set_tests_properties(ra2.lifecycle PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence")
    add_test(NAME ra2.entry COMMAND ra2 --entry-regression)
    add_test(NAME ra2.reference_map COMMAND ra2 --reference-map-regression ${CMAKE_BINARY_DIR}/reference-map-audit)
    add_test(NAME ra2.placement COMMAND ra2 --placement-regression ${CMAKE_BINARY_DIR}/placement-audit)
    add_test(NAME ra2.production_feedback COMMAND ra2 --production-feedback-regression ${CMAKE_BINARY_DIR}/production-feedback-audit)
    add_test(NAME ra2.refund COMMAND ra2 --refund-regression ${CMAKE_BINARY_DIR}/refund-audit)
    add_test(NAME ra2.siege_pose COMMAND ra2 --siege-pose-regression ${CMAKE_BINARY_DIR}/siege-pose-audit)
    add_test(NAME ra2.voxel_animation COMMAND ra2 --voxel-animation-regression ${CMAKE_BINARY_DIR}/voxel-animation-audit)
    add_test(NAME ra2.chopper_transition COMMAND ra2 --chopper-transition-regression ${CMAKE_BINARY_DIR}/chopper-transition-audit)
    add_test(NAME ra2.elevation_feedback COMMAND ra2 --elevation-feedback-regression ${CMAKE_BINARY_DIR}/elevation-feedback-audit)
    set_tests_properties(ra2.elevation_feedback PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;screenshot;interaction;determinism" RUN_SERIAL TRUE)
    set_tests_properties(ra2.chopper_transition PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;screenshot;interaction;determinism;persistence" RUN_SERIAL TRUE)
    set_tests_properties(ra2.voxel_animation PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;screenshot;interaction;determinism" RUN_SERIAL TRUE)
    set_tests_properties(ra2.siege_pose PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;screenshot;interaction;determinism;persistence" RUN_SERIAL TRUE)
    add_test(NAME ra2.terrain_visual COMMAND ra2 --terrain-visual-regression ${CMAKE_BINARY_DIR}/terrain-audit)
    add_test(NAME ra2.generated_map COMMAND ra2 --generated-map-structure ${CMAKE_BINARY_DIR}/generated-map-audit)
    set_tests_properties(ra2.generated_map PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;map;determinism;presentation;screenshot")
    add_test(NAME ra2.generated_health COMMAND ra2 --generated-map-health)
    set_tests_properties(ra2.generated_health PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 LABELS "natural-start;map;economy;determinism;persistence")
    add_test(NAME ra2.factory_state COMMAND ra2 --factory-state-regression ${CMAKE_BINARY_DIR}/factory-state-audit)
    set_tests_properties(ra2.factory_state PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;economy;interaction;determinism;persistence;presentation;screenshot")
    add_test(NAME ra2.chrono_visual COMMAND ra2 --chrono-visual-regression ${CMAKE_BINARY_DIR}/chrono-audit)
    set_tests_properties(ra2.chrono_visual PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;interaction;determinism;persistence;screenshot")
    add_test(NAME ra2.superweapon COMMAND ra2 --superweapon-regression ${CMAKE_BINARY_DIR}/superweapon-audit)
    set_tests_properties(ra2.superweapon PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;interaction;determinism;persistence;presentation;screenshot")
    add_test(NAME ra2.fog_feedback COMMAND ra2 --fog-feedback-regression ${CMAKE_BINARY_DIR}/fog-feedback-audit)
    set_tests_properties(ra2.fog_feedback PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;interaction;determinism;screenshot")
    add_test(NAME ra2.fog_knowledge COMMAND ra2 --fog-knowledge-regression ${CMAKE_BINARY_DIR}/fog-knowledge-audit)
    set_tests_properties(ra2.fog_knowledge PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;presentation;interaction;determinism;persistence;screenshot")
    set_tests_properties(ra2.terrain_visual PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "presentation;interaction;determinism;screenshot")
    set_tests_properties(ra2.refund PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;economy;determinism;persistence;presentation;screenshot")
    set_tests_properties(ra2.production_feedback PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;presentation;screenshot")
    set_tests_properties(ra2.placement PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence;presentation;screenshot")
    set_tests_properties(ra2.reference_map PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "natural-start;interaction;determinism;persistence;screenshot")
    set_tests_properties(ra2.entry PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism")
    add_test(NAME ra2.service COMMAND ra2 --service-regression)
    set_tests_properties(ra2.service PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence")
    add_test(NAME ra2.instant_combat COMMAND ra2 --instant-combat-regression)
    set_tests_properties(ra2.instant_combat PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence")
    add_test(NAME ra2.release COMMAND ra2 --release-regression)
    set_tests_properties(ra2.release PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence")
    set_tests_properties(ra2.movement PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;performance")
    add_test(NAME ra2.endurance COMMAND ra2 --skirmish-regression 36000)
    set_tests_properties(ra2.endurance PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 LABELS "endurance;natural-start")
    add_test(NAME ra2.logic COMMAND ra2_logic_tests)
    add_test(NAME ra2.app_state COMMAND ra2 --app-state-test)
    set_tests_properties(ra2.app_state PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 LABELS "determinism;persistence" RUN_SERIAL TRUE)
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
    set_tests_properties(ra2.logic ra2.state PROPERTIES LABELS "mechanism;determinism")
    set_tests_properties(ra2.smoke PROPERTIES LABELS "mechanism;screenshot")
    set_tests_properties(ra2.campaign_matrix PROPERTIES LABELS "campaign;mechanism")
    if(WIN32)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME ra2.animation_matrix COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/animation_matrix.py
            --exe $<TARGET_FILE:ra2> --output ${CMAKE_BINARY_DIR}/animation-audit)
        set_tests_properties(ra2.animation_matrix PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;presentation;determinism")
        add_test(NAME ra2.chrono_pipeline COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/chrono_fx_pipeline_test.py)
        set_tests_properties(ra2.chrono_pipeline PROPERTIES TIMEOUT 15 LABELS "assets;format;presentation")
        add_test(NAME ra2.chrono_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/chrono_asset_matrix.py --exe $<TARGET_FILE:ra2>)
        set_tests_properties(ra2.chrono_assets PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;presentation;validation")
        add_test(NAME ra2.asset_pipeline COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/asset_pipeline_test.py)
        add_test(NAME ra2.voxel_projection COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/voxel_projection_matrix.py --exe $<TARGET_FILE:ra2> --output ${CMAKE_BINARY_DIR}/voxel-projection-audit)
        add_test(NAME ra2.chopper_pipeline COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/chopper_deploy_pipeline_test.py)
        set_tests_properties(ra2.chopper_pipeline PROPERTIES TIMEOUT 15 LABELS "assets;format;presentation")
        set_tests_properties(ra2.voxel_projection PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;format;presentation;validation")
        set_tests_properties(ra2.asset_pipeline PROPERTIES TIMEOUT 15 LABELS "assets;format")
        add_test(NAME ra2.tmp_decode COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/tmp_decode_test.py)
        set_tests_properties(ra2.tmp_decode PROPERTIES TIMEOUT 15 LABELS "assets;format")
        add_test(NAME ra2.cameo_pipeline COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/cameo_pipeline_test.py)
        set_tests_properties(ra2.cameo_pipeline PROPERTIES TIMEOUT 15 LABELS "assets;presentation")
        add_test(NAME ra2.hud_matrix COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/hud_matrix.py
            --exe $<TARGET_FILE:ra2> --output ${CMAKE_BINARY_DIR}/hud-audit)
        set_tests_properties(ra2.hud_matrix PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "presentation;interaction;screenshot")
        add_test(NAME ra2.visual_matrix COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/visual_matrix.py
            --exe $<TARGET_FILE:ra2> --output ${CMAKE_BINARY_DIR}/visual-audit)
        set_tests_properties(ra2.visual_matrix PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120 RUN_SERIAL TRUE LABELS "presentation;interaction;performance")
        add_test(NAME ra2.presentation_matrix
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/presentation_matrix.py --exe $<TARGET_FILE:ra2>)
        set_tests_properties(ra2.presentation_matrix PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "determinism;presentation")
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
    # MSVC's instrumented executables need the runtime shipped beside cl.exe.
    # Discover it from the selected compiler, so every CTest subprocess also
    # works from an ordinary shell without a manually edited PATH.
    if(MSVC AND RA2_ASAN)
        get_filename_component(RA2_ASAN_RUNTIME_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(RA2_ASAN_RUNTIME_NAME clang_rt.asan_dynamic-x86_64.dll)
        else()
            set(RA2_ASAN_RUNTIME_NAME clang_rt.asan_dynamic-i386.dll)
        endif()
        if(NOT EXISTS "${RA2_ASAN_RUNTIME_DIRECTORY}/${RA2_ASAN_RUNTIME_NAME}")
            message(FATAL_ERROR "MSVC ASan runtime not found beside the selected compiler: ${RA2_ASAN_RUNTIME_DIRECTORY}/${RA2_ASAN_RUNTIME_NAME}")
        endif()
        get_property(RA2_ASAN_TESTS DIRECTORY PROPERTY TESTS)
        set_tests_properties(${RA2_ASAN_TESTS} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RA2_ASAN_RUNTIME_DIRECTORY}")
    endif()

endif()
