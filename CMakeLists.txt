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
file(GLOB_RECURSE RA2_REGRESSION_SOURCES CONFIGURE_DEPENDS src/*_tests.cpp)
list(APPEND RA2_REGRESSION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/game_regression.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/game_live_verify.cpp)
list(FILTER RA2_SOURCES EXCLUDE REGEX "_tests[.]cpp$")
list(REMOVE_ITEM RA2_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/game_regression.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/game_live_verify.cpp)

option(RA2_ASAN "Enable AddressSanitizer" OFF)
function(ra2_configure_compile target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /MP /utf-8 /permissive- /Zc:__cplusplus)
        if(RA2_ASAN)
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            target_compile_definitions(${target} PUBLIC RA2_ASAN_ACTIVE=1)
        endif()
    endif()
endfunction()
function(ra2_configure_executable target)
    ra2_configure_compile(${target})
    if(MSVC)
        # GUI subsystem avoids parent-console close events killing a match.
        target_link_options(${target} PRIVATE /Brepro /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
        if(RA2_ASAN)
            target_link_options(${target} PRIVATE /INFERASANLIBS /INCREMENTAL:NO /DEBUG)
        endif()
    endif()
endfunction()

# Runtime objects are identical with testing enabled or disabled. Tests never
# add preprocessor branches or test source dependencies to this library.
add_library(ra2_runtime STATIC ${RA2_SOURCES})
target_include_directories(ra2_runtime PUBLIC src)
target_link_libraries(ra2_runtime PUBLIC raylib lua_static)
ra2_configure_compile(ra2_runtime)
add_executable(ra2 src/main.cpp)
target_link_libraries(ra2 PRIVATE ra2_runtime)
ra2_configure_executable(ra2)
if(MSVC AND RA2_ASAN)
    target_compile_options(raylib PRIVATE /fsanitize=address)
endif()

# 无窗口纯逻辑基线：不链接 raylib，不初始化图形或音频设备。
if(BUILD_TESTING)
    add_executable(ra2_regression src/main.cpp ${RA2_REGRESSION_SOURCES} tests/game_regression.h)
    target_include_directories(ra2_regression PRIVATE tests)
    target_link_libraries(ra2_regression PRIVATE ra2_runtime)
    ra2_configure_executable(ra2_regression)
    add_executable(ra2_hash_tests tests/hash_tests.cpp src/core/sha256.cpp)
    target_include_directories(ra2_hash_tests PRIVATE src)
    if(MSVC)
        target_compile_options(ra2_hash_tests PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus)
        if(RA2_ASAN)
            target_compile_options(ra2_hash_tests PRIVATE /fsanitize=address /Zi)
            target_link_options(ra2_hash_tests PRIVATE /INFERASANLIBS /INCREMENTAL:NO /DEBUG)
        endif()
    endif()
    add_test(NAME ra2.hash COMMAND ra2_hash_tests)
    set_tests_properties(ra2.hash PROPERTIES TIMEOUT 30 LABELS "logic;network;determinism")
    add_executable(ra2_rules_tests tests/rules_fingerprint_tests.cpp src/game/data.cpp src/game/data_fingerprint.cpp src/core/content.cpp src/core/sha256.cpp)
    target_include_directories(ra2_rules_tests PRIVATE src)
    target_link_libraries(ra2_rules_tests PRIVATE raylib)
    if(MSVC)
        target_compile_options(ra2_rules_tests PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus)
        if(RA2_ASAN)
            target_compile_options(ra2_rules_tests PRIVATE /fsanitize=address /Zi)
            target_link_options(ra2_rules_tests PRIVATE /INFERASANLIBS /INCREMENTAL:NO /DEBUG)
        endif()
    endif()
    add_test(NAME ra2.rules_fingerprint COMMAND ra2_rules_tests ${CMAKE_BINARY_DIR}/rules-fingerprint-audit)
    set_tests_properties(ra2.rules_fingerprint PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "logic;network;determinism;rules")
    target_sources(ra2_regression PRIVATE tests/legacy_v18_writer.cpp tests/legacy_v19_writer.cpp tests/legacy_v20_writer.cpp tests/legacy_v21_writer.cpp tests/legacy_v22_writer.cpp tests/legacy_v7_fixture.cpp)
    target_compile_definitions(ra2_regression PRIVATE RA2_TESTING)
    add_executable(ra2_jumpjet_tests tests/native_jumpjet_tests.cpp src/game/native_jumpjet.cpp src/game/native_flight.cpp)
    target_include_directories(ra2_jumpjet_tests PRIVATE src tests)
    if(MSVC)
        target_compile_options(ra2_jumpjet_tests PRIVATE /W4 /utf-8 /permissive-)
        if(RA2_ASAN)
            target_compile_options(ra2_jumpjet_tests PRIVATE /fsanitize=address /Zi)
            target_link_options(ra2_jumpjet_tests PRIVATE /INFERASANLIBS /INCREMENTAL:NO /DEBUG)
        endif()
    endif()
    add_test(NAME ra2.jumpjet_math COMMAND ra2_jumpjet_tests)
    add_executable(ra2_air_clearance_tests tests/native_air_clearance_tests.cpp src/game/native_jumpjet.cpp src/game/native_flight.cpp)
    target_include_directories(ra2_air_clearance_tests PRIVATE src tests)
    if(MSVC)
        target_compile_options(ra2_air_clearance_tests PRIVATE /W4 /utf-8 /permissive-)
        if(RA2_ASAN)
            target_compile_options(ra2_air_clearance_tests PRIVATE /fsanitize=address /Zi)
            target_link_options(ra2_air_clearance_tests PRIVATE /INFERASANLIBS /INCREMENTAL:NO /DEBUG)
        endif()
    endif()
    add_test(NAME ra2.air_clearance COMMAND ra2_air_clearance_tests)
    set_tests_properties(ra2.air_clearance PROPERTIES TIMEOUT 30 LABELS "logic;determinism;movement")
    set_tests_properties(ra2.jumpjet_math PROPERTIES TIMEOUT 30 LABELS "logic;determinism;movement")
    add_executable(ra2_logic_tests
        tests/logic_tests.cpp
        src/game/map.cpp
        src/game/map_bridges.cpp
        src/game/map_startup.cpp
    )
    target_include_directories(ra2_logic_tests PRIVATE src)
    if(MSVC)
        target_compile_options(ra2_logic_tests PRIVATE /W4 /MP /utf-8 /permissive- /Zc:__cplusplus)
        target_link_options(ra2_logic_tests PRIVATE /Brepro)
    endif()
    add_test(NAME ra2.input COMMAND ra2_regression --play-test)
    set_tests_properties(ra2.input PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "interaction;scripted" RUN_SERIAL TRUE)
    add_test(NAME ra2.economy COMMAND ra2_regression --economy-regression)
    set_tests_properties(ra2.economy PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;recovery")
    add_test(NAME ra2.movement COMMAND ra2_regression --movement-regression)
    add_test(NAME ra2.group_orders COMMAND ra2_regression --group-order-regression)
    add_test(NAME ra2.rules_profile COMMAND ra2_regression --rules-profile-regression ${CMAKE_BINARY_DIR}/rules-profile-audit)
    add_test(NAME ra2.rules_selection COMMAND ra2_regression --rules-selection-regression ${CMAKE_BINARY_DIR}/rules-selection-audit)
    set_tests_properties(ra2.rules_selection PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240
        RUN_SERIAL TRUE SKIP_RETURN_CODE 77 LABELS "rules;interaction;persistence;network;assets")
    add_test(NAME ra2.rules_spawners COMMAND ra2_regression --rules-spawner-regression ${CMAKE_BINARY_DIR}/rules-spawner-audit)
    add_test(NAME ra2.aircraft_spawners COMMAND ra2_regression --aircraft-spawner-regression ${CMAKE_BINARY_DIR}/aircraft-spawner-audit)
    add_test(NAME ra2.asw_spawners COMMAND ra2_regression --asw-spawner-regression ${CMAKE_BINARY_DIR}/asw-spawner-audit)
    set_tests_properties(ra2.asw_spawners PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "mechanism;combat;assets;save")
    set_tests_properties(ra2.aircraft_spawners PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "rules;combat;persistence;determinism;production;presentation;interaction")
    set_tests_properties(ra2.rules_spawners PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "rules;assets;combat;persistence;determinism;production;presentation")
    set_tests_properties(ra2.rules_profile PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "rules;assets;determinism;persistence;production;combat")
    set_tests_properties(ra2.group_orders PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;movement;interaction;performance;determinism" RUN_SERIAL TRUE)
    foreach(combat_case RANGE 0 3)
        add_test(NAME ra2.combat_pressure_${combat_case} COMMAND ra2_regression --combat-pressure-regression ${combat_case} ${CMAKE_BINARY_DIR}/combat-pressure-audit)
        set_tests_properties(ra2.combat_pressure_${combat_case} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;combat;performance;determinism;persistence")
        if(combat_case GREATER 1)
            set_property(TEST ra2.combat_pressure_${combat_case} APPEND PROPERTY LABELS "presentation;screenshot")
        endif()
    endforeach()
    add_test(NAME ra2.traffic COMMAND ra2_regression --traffic-regression)
    # Keep instrumented finite traffic cases independent: combining all four
    # spent the aggregate timeout before the last case could finish.
    if(MSVC AND RA2_ASAN)
        foreach(junction_case RANGE 0 7)
            add_test(NAME ra2.junction_${junction_case} COMMAND ra2_regression --junction-regression ${junction_case})
            set_tests_properties(ra2.junction_${junction_case} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 LABELS "mechanism;movement;recovery;determinism;persistence")
        endforeach()
    else()
        add_test(NAME ra2.junction COMMAND ra2_regression --junction-regression)
        set_tests_properties(ra2.junction PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;movement;recovery;determinism;persistence")
        foreach(junction_case RANGE 4 7)
            add_test(NAME ra2.junction_${junction_case} COMMAND ra2_regression --junction-regression ${junction_case})
            set_tests_properties(ra2.junction_${junction_case} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;movement;recovery;determinism;persistence")
        endforeach()
    endif()
    add_test(NAME ra2.blockers COMMAND ra2_regression --blocker-regression)
    add_test(NAME ra2.idle_yield COMMAND ra2_regression --idle-yield-regression)
    add_test(NAME ra2.chopper_ground COMMAND ra2_regression --chopper-ground-regression)
    add_test(NAME ra2.immobilized COMMAND ra2_regression --immobilized-regression)
    add_test(NAME ra2.deployment_feedback COMMAND ra2_regression --deployment-feedback-regression)
    add_test(NAME ra2.rally COMMAND ra2_regression --rally-regression)
    add_test(NAME ra2.rally_visual COMMAND ra2_regression --rally-visual-regression ${CMAKE_BINARY_DIR}/rally-visual-audit)
    set_tests_properties(ra2.rally_visual PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "presentation;interaction;screenshot;determinism" RUN_SERIAL TRUE)
    add_test(NAME ra2.refinery_recovery COMMAND ra2_regression --refinery-recovery-regression)
    set_tests_properties(ra2.refinery_recovery PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;recovery;economy")
    set_tests_properties(ra2.rally PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence;recovery")
    add_test(NAME ra2.feedback_visual COMMAND ra2_regression --feedback-visual-regression ${CMAKE_BINARY_DIR}/feedback-visual-audit)
    set_tests_properties(ra2.feedback_visual PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "presentation;interaction;screenshot;determinism" RUN_SERIAL TRUE)
    set_tests_properties(ra2.deployment_feedback PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;presentation;determinism")
    add_test(NAME ra2.magnetic_landing COMMAND ra2_regression --magnetic-landing-regression ${CMAKE_BINARY_DIR}/magnetic-landing-audit)
    set_tests_properties(ra2.magnetic_landing PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;interaction")
    set_tests_properties(ra2.immobilized PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;interaction;recovery")
    set_tests_properties(ra2.chopper_ground PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;interaction")
    set_tests_properties(ra2.idle_yield PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;determinism;persistence;recovery")
    set_tests_properties(ra2.blockers PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence;recovery")
    add_test(NAME ra2.ai_attack COMMAND ra2_regression --ai-attack-regression)
    add_test(NAME ra2.ai_scouting COMMAND ra2_regression --ai-scouting-regression ${CMAKE_BINARY_DIR}/ai-gap-audit)
    add_test(NAME ra2.gap_height COMMAND ra2_regression --gap-height-regression ${CMAKE_BINARY_DIR}/gap-height-audit)
    set_tests_properties(ra2.gap_height PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;fog;determinism;persistence" RUN_SERIAL TRUE)
    add_test(NAME ra2.gap_slope COMMAND ra2_regression --gap-slope-regression ${CMAKE_BINARY_DIR}/gap-slope-audit)
    set_tests_properties(ra2.gap_slope PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;fog;determinism;persistence;native-map" RUN_SERIAL TRUE)
    add_test(NAME ra2.source_transport COMMAND ra2_regression --source-transport-regression ${CMAKE_BINARY_DIR}/source-transport-audit)
    set_tests_properties(ra2.source_transport PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;transport;interaction;lifecycle;determinism;persistence" RUN_SERIAL TRUE)
    add_test(NAME ra2.cargo_ownership COMMAND ra2_regression --cargo-ownership-regression ${CMAKE_BINARY_DIR}/cargo-ownership-audit)
    set_tests_properties(ra2.cargo_ownership PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;transport;ownership;lifecycle;determinism;persistence" RUN_SERIAL TRUE)
    add_test(NAME ra2.ground_unload COMMAND ra2_regression --ground-unload-regression ${CMAKE_BINARY_DIR}/ground-unload-audit)
    set_tests_properties(ra2.ground_unload PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;transport;timing;lifecycle;determinism;persistence" RUN_SERIAL TRUE)
    add_test(NAME ra2.entity_identity COMMAND ra2_regression --entity-identity-regression --mod ${CMAKE_SOURCE_DIR}/tests/fixtures/entity-identity)
    add_test(NAME ra2.transport_exit COMMAND ra2_regression --transport-exit-regression ${CMAKE_BINARY_DIR}/transport-exit-audit)
    add_test(NAME ra2.allied_boarding COMMAND ra2_regression --allied-boarding-regression ${CMAKE_BINARY_DIR}/allied-boarding-audit)
    add_test(NAME ra2.transport_boarding COMMAND ra2_regression --transport-boarding-regression ${CMAKE_BINARY_DIR}/transport-boarding-audit)
    set_tests_properties(ra2.transport_boarding PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;transport;ground;interaction;lifecycle;determinism;persistence" RUN_SERIAL TRUE)
    set_tests_properties(ra2.allied_boarding PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;transport;ownership;input;lifecycle;determinism;persistence" RUN_SERIAL TRUE)
    set_tests_properties(ra2.transport_exit PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 LABELS "mechanism;transport;ground;turning;lifecycle;determinism;persistence" RUN_SERIAL TRUE)
    set_tests_properties(ra2.entity_identity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 LABELS "network;determinism;persistence;interaction;lifecycle" RUN_SERIAL TRUE)
    set_tests_properties(ra2.ai_scouting PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 LABELS "mechanism;ai;determinism;persistence")
    add_test(NAME ra2.magnetron COMMAND ra2_regression --magnetron-regression ${CMAKE_BINARY_DIR}/magnetron-audit)
    set_tests_properties(ra2.magnetron PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "mechanism;combat;determinism;persistence;presentation;screenshot")
    set_tests_properties(ra2.ai_attack PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;ai;determinism;persistence;movement")
    add_test(NAME ra2.ai_economy COMMAND ra2_regression --ai-economy-regression)
    set_tests_properties(ra2.ai_economy PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;ai;economy;recovery;determinism;persistence")
    add_test(NAME ra2.ai_construction COMMAND ra2_regression --ai-construction-regression ${CMAKE_BINARY_DIR}/ai-construction-audit)
    set_tests_properties(ra2.ai_construction PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 360 LABELS "mechanism;ai;economy;recovery;movement;determinism;persistence")
    add_test(NAME ra2.ai_air_defense COMMAND ra2_regression --ai-air-defense-regression)
    set_tests_properties(ra2.ai_air_defense PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;ai;interaction;movement;recovery;determinism;persistence")
    add_test(NAME ra2.ai_factory_exit COMMAND ra2_regression --ai-factory-exit-regression)
    set_tests_properties(ra2.ai_factory_exit PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;ai;production;recovery;movement;determinism;persistence")
    add_test(NAME ra2.ai_transport COMMAND ra2_regression --ai-transport-regression)
    add_test(NAME ra2.ai_air_transport COMMAND ra2_regression --ai-air-transport-regression ${CMAKE_BINARY_DIR}/ai-air-transport-audit)
    add_test(NAME ra2.source_modes COMMAND ra2_regression --source-mode-regression ${CMAKE_BINARY_DIR}/source-mode-audit)
    add_test(NAME ra2.source_economy COMMAND ra2_regression --source-economy-regression ${CMAKE_BINARY_DIR}/source-economy-audit)
    add_test(NAME ra2.building_repair COMMAND ra2_regression --building-repair-regression ${CMAKE_BINARY_DIR}/building-repair-audit)
    add_test(NAME ra2.building_sale COMMAND ra2_regression --building-sale-regression ${CMAKE_BINARY_DIR}/building-sale-audit)
    add_test(NAME ra2.building_survivors COMMAND ra2_regression --building-survivors-regression ${CMAKE_BINARY_DIR}/building-survivors-audit)
    set_tests_properties(ra2.building_survivors PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;economy;lifecycle;persistence;determinism")
    add_test(NAME ra2.player_scatter COMMAND ra2_regression --player-scatter-regression ${CMAKE_BINARY_DIR}/player-scatter-audit)
    set_tests_properties(ra2.player_scatter PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;movement;lifecycle;persistence;determinism")
    add_test(NAME ra2.infantry_travel COMMAND ra2_regression --infantry-travel-regression ${CMAKE_BINARY_DIR}/infantry-travel-audit)
    set_tests_properties(ra2.infantry_travel PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;movement;lifecycle;persistence;determinism")
    set_tests_properties(ra2.building_sale PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;economy;interaction;lifecycle;persistence;determinism")
    set_tests_properties(ra2.building_repair PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;economy;interaction;persistence;determinism")
    add_test(NAME ra2.ground_control COMMAND ra2_regression --ground-control-regression ${CMAKE_BINARY_DIR}/ground-control-audit)
    set_tests_properties(ra2.ground_control PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;economy;movement;combat;persistence;determinism;presentation")
    add_test(NAME ra2.ground_travel COMMAND ra2_regression --ground-travel-regression ${CMAKE_BINARY_DIR}/ground-travel-audit)
    add_test(NAME ra2.crushing COMMAND ra2_regression --crushing-regression ${CMAKE_BINARY_DIR}/crushing-audit)
    set_tests_properties(ra2.crushing PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;movement;combat;lifecycle;persistence;determinism")
    add_test(NAME ra2.drive_blockage COMMAND ra2_regression --drive-blockage-regression ${CMAKE_BINARY_DIR}/drive-blockage-audit)
    set_tests_properties(ra2.drive_blockage PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;movement;interaction;persistence;determinism")
    set_tests_properties(ra2.ground_travel PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "economy;movement;persistence;determinism")
    add_test(NAME ra2.source_support COMMAND ra2_regression --source-support-regression ${CMAKE_BINARY_DIR}/source-support-audit --mod ${CMAKE_SOURCE_DIR}/tests/fixtures/source-support-script)
    add_test(NAME ra2.support_delivery COMMAND ra2_regression --support-delivery-regression ${CMAKE_BINARY_DIR}/support-delivery-audit)
    set_tests_properties(ra2.support_delivery PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE LABELS "rules;abilities;combat;persistence;determinism;presentation")
    set_tests_properties(ra2.source_support PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE LABELS "rules;abilities;economy;interaction;persistence;determinism;presentation")
    set_tests_properties(ra2.source_economy PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;rules;economy;espionage;production;persistence;determinism")
    add_test(NAME ra2.source_crates COMMAND ra2_regression --source-crate-regression ${CMAKE_BINARY_DIR}/source-crate-audit)
    add_test(NAME ra2.rules_consistency COMMAND ra2_regression --rules-consistency-regression ${CMAKE_BINARY_DIR}/rules-consistency-audit)
    set_tests_properties(ra2.rules_consistency PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "rules;production;interaction;persistence;determinism;presentation")
    set_tests_properties(ra2.source_crates PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE LABELS "assets;rules;crates;determinism;persistence")
    add_test(NAME ra2.native_gatling COMMAND ra2_regression --native-gatling-regression ${CMAKE_BINARY_DIR}/native-gatling-audit)
    set_tests_properties(ra2.native_gatling PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;rules;combat;weapons;determinism;persistence")
    set_tests_properties(ra2.source_modes PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE LABELS "assets;rules;production;modes;determinism;persistence")
    foreach(edition ra2 yr)
        if(edition STREQUAL "ra2")
            set(source_edition 0)
        else()
            set(source_edition 1)
        endif()
        foreach(terrain 0 1)
            # Real source battles can run past 180k ticks: the armed SREF case
            # naturally settles at 189081. Keep the outcome requirement and
            # allow a 240k observation window; never manufacture a winner.
            add_test(NAME ra2.source_match_${edition}_${terrain} COMMAND ra2_regression --source-match-regression ${CMAKE_BINARY_DIR}/source-match-audit 360000 ${source_edition} ${terrain})
            set_tests_properties(ra2.source_match_${edition}_${terrain} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 RUN_SERIAL TRUE LABELS "assets;ai;rules;natural;determinism;persistence")
        endforeach()
    endforeach()
    set_tests_properties(ra2.ai_air_transport PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE LABELS "assets;ai;rules;production;movement;interaction;determinism;persistence")
    set_tests_properties(ra2.ai_transport PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;ai;movement;interaction;determinism;persistence")
    add_test(NAME ra2.moving_yield COMMAND ra2_regression --moving-yield-regression)
    set_tests_properties(ra2.moving_yield PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;movement;recovery;determinism;persistence")
    add_test(NAME ra2.generated_match_recovery COMMAND ra2_regression --generated-match 36000 2 305419896 3)
    set_tests_properties(ra2.generated_match_recovery PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 LABELS "natural-start;full-match;ai;economy;movement;recovery;determinism;persistence")
    add_test(NAME ra2.match_completion COMMAND ra2_regression --skirmish-regression 72000 -1 0 --require-outcomes)
    set_tests_properties(ra2.match_completion PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 LABELS "natural-start;full-match;determinism;persistence")
    set_tests_properties(ra2.traffic PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;determinism;recovery;persistence")
    if(MSVC AND RA2_ASAN)
        # Keep the complete traffic/replay checks under ASan instrumentation.
        # This only extends process time; movement and tick budgets stay fixed.
        set_tests_properties(ra2.traffic PROPERTIES TIMEOUT 600)
    endif()
    add_test(NAME ra2.attack COMMAND ra2_regression --attack-regression)
    add_test(NAME ra2.apocalypse COMMAND ra2_regression --apocalypse-regression)
    add_test(NAME ra2.burst COMMAND ra2_regression --burst-regression)
    add_test(NAME ra2.splash COMMAND ra2_regression --splash-regression)
    set_tests_properties(ra2.splash PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;combat;determinism;persistence")
    set_tests_properties(ra2.burst PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;combat;determinism;persistence")
    set_tests_properties(ra2.apocalypse PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;combat;determinism;persistence")
    set_tests_properties(ra2.attack PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism")
    add_test(NAME ra2.engineer COMMAND ra2_regression --engineer-regression)
    set_tests_properties(ra2.engineer PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence")
    add_test(NAME ra2.lifecycle COMMAND ra2_regression --lifecycle-regression)
    set_tests_properties(ra2.lifecycle PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence")
    add_test(NAME ra2.entry COMMAND ra2_regression --entry-regression)
    add_test(NAME ra2.reference_map COMMAND ra2_regression --reference-map-regression ${CMAKE_BINARY_DIR}/reference-map-audit)
    add_test(NAME ra2.placement COMMAND ra2_regression --placement-regression ${CMAKE_BINARY_DIR}/placement-audit)
    add_test(NAME ra2.production_feedback COMMAND ra2_regression --production-feedback-regression ${CMAKE_BINARY_DIR}/production-feedback-audit)
    add_test(NAME ra2.refund COMMAND ra2_regression --refund-regression ${CMAKE_BINARY_DIR}/refund-audit)
    add_test(NAME ra2.siege_pose COMMAND ra2_regression --siege-pose-regression ${CMAKE_BINARY_DIR}/siege-pose-audit)
    add_test(NAME ra2.voxel_animation COMMAND ra2_regression --voxel-animation-regression ${CMAKE_BINARY_DIR}/voxel-animation-audit)
    add_test(NAME ra2.chopper_transition COMMAND ra2_regression --chopper-transition-regression ${CMAKE_BINARY_DIR}/chopper-transition-audit)
    add_test(NAME ra2.elevation_feedback COMMAND ra2_regression --elevation-feedback-regression ${CMAKE_BINARY_DIR}/elevation-feedback-audit)
    set_tests_properties(ra2.elevation_feedback PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;screenshot;interaction;determinism" RUN_SERIAL TRUE)
    set_tests_properties(ra2.chopper_transition PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;screenshot;interaction;determinism;persistence" RUN_SERIAL TRUE)
    set_tests_properties(ra2.voxel_animation PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;screenshot;interaction;determinism" RUN_SERIAL TRUE)
    set_tests_properties(ra2.siege_pose PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;screenshot;interaction;determinism;persistence" RUN_SERIAL TRUE)
    add_test(NAME ra2.terrain_visual COMMAND ra2_regression --terrain-visual-regression ${CMAKE_BINARY_DIR}/terrain-audit)
    add_test(NAME ra2.generated_map COMMAND ra2_regression --generated-map-structure ${CMAKE_BINARY_DIR}/generated-map-audit)
    set_tests_properties(ra2.generated_map PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;map;determinism;presentation;screenshot")
    add_test(NAME ra2.generated_health COMMAND ra2_regression --generated-map-health)
    set_tests_properties(ra2.generated_health PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 LABELS "natural-start;map;economy;determinism;persistence")
    add_test(NAME ra2.factory_state COMMAND ra2_regression --factory-state-regression ${CMAKE_BINARY_DIR}/factory-state-audit)
    set_tests_properties(ra2.factory_state PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;economy;interaction;determinism;persistence;presentation;screenshot")
    add_test(NAME ra2.chrono_visual COMMAND ra2_regression --chrono-visual-regression ${CMAKE_BINARY_DIR}/chrono-audit)
    set_tests_properties(ra2.chrono_visual PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;interaction;determinism;persistence;screenshot")
    add_test(NAME ra2.superweapon COMMAND ra2_regression --superweapon-regression ${CMAKE_BINARY_DIR}/superweapon-audit)
    set_tests_properties(ra2.superweapon PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;interaction;determinism;persistence;presentation;screenshot")
    add_test(NAME ra2.fog_feedback COMMAND ra2_regression --fog-feedback-regression ${CMAKE_BINARY_DIR}/fog-feedback-audit)
    set_tests_properties(ra2.fog_feedback PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "mechanism;presentation;interaction;determinism;screenshot")
    add_test(NAME ra2.fog_knowledge COMMAND ra2_regression --fog-knowledge-regression ${CMAKE_BINARY_DIR}/fog-knowledge-audit)
    set_tests_properties(ra2.fog_knowledge PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;presentation;interaction;determinism;persistence;screenshot")
    set_tests_properties(ra2.terrain_visual PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "presentation;interaction;determinism;screenshot")
    set_tests_properties(ra2.refund PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;economy;determinism;persistence;presentation;screenshot")
    set_tests_properties(ra2.production_feedback PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;presentation;screenshot")
    set_tests_properties(ra2.placement PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence;presentation;screenshot")
    set_tests_properties(ra2.reference_map PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "natural-start;interaction;determinism;persistence;screenshot")
    set_tests_properties(ra2.entry PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism")
    add_test(NAME ra2.service COMMAND ra2_regression --service-regression)
    set_tests_properties(ra2.service PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence")
    add_test(NAME ra2.instant_combat COMMAND ra2_regression --instant-combat-regression)
    set_tests_properties(ra2.instant_combat PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;determinism;persistence")
    add_test(NAME ra2.release COMMAND ra2_regression --release-regression)
    set_tests_properties(ra2.release PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;interaction;determinism;persistence")
    set_tests_properties(ra2.movement PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "mechanism;performance")
    add_test(NAME ra2.endurance COMMAND ra2_regression --skirmish-regression 36000)
    set_tests_properties(ra2.endurance PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 LABELS "endurance;natural-start")
    add_test(NAME ra2.logic COMMAND ra2_logic_tests)
    add_test(NAME ra2.app_state COMMAND ra2_regression --app-state-test)
    add_test(NAME ra2.unit_voices COMMAND ra2_regression --unit-voice-regression)
    add_test(NAME ra2.native_audio COMMAND ra2_regression --native-audio-regression)
    add_test(NAME ra2.native_map COMMAND ra2_regression --native-map-regression ${CMAKE_BINARY_DIR}/native-map-audit)
    add_test(NAME ra2.native_scene COMMAND ra2_regression --native-scene-regression ${CMAKE_BINARY_DIR}/native-scene-audit)
    add_test(NAME ra2.native_bridge COMMAND ra2_regression --native-bridge-regression ${CMAKE_BINARY_DIR}/native-bridge-audit)
    add_test(NAME ra2.native_bridge_repair COMMAND ra2_regression --native-bridge-repair-regression ${CMAKE_BINARY_DIR}/native-bridge-repair-audit)
    add_test(NAME ra2.native_gates COMMAND ra2_regression --native-gate-regression ${CMAKE_BINARY_DIR}/native-gates-audit)
    add_test(NAME ra2.native_events COMMAND ra2_regression --native-event-regression ${CMAKE_BINARY_DIR}/native-events-audit)
    add_test(NAME ra2.native_teams COMMAND ra2_regression --native-team-regression ${CMAKE_BINARY_DIR}/native-teams-audit)
    add_test(NAME ra2.native_team_orders COMMAND ra2_regression --native-team-orders-regression ${CMAKE_BINARY_DIR}/native-team-orders-audit)
    add_test(NAME ra2.native_team_attack COMMAND ra2_regression --native-team-attack-regression ${CMAKE_BINARY_DIR}/native-team-attack-audit)
    add_test(NAME ra2.native_weapons COMMAND ra2_regression --native-weapon-regression ${CMAKE_BINARY_DIR}/native-weapons-audit)
    add_test(NAME ra2.native_weapon_actions COMMAND ra2_regression --native-weapon-actions-regression ${CMAKE_BINARY_DIR}/native-weapon-actions-audit)
    add_test(NAME ra2.native_shrapnel COMMAND ra2_regression --native-shrapnel-regression ${CMAKE_BINARY_DIR}/native-shrapnel-audit)
    add_test(NAME ra2.native_ifv COMMAND ra2_regression --native-ifv-regression ${CMAKE_BINARY_DIR}/native-ifv-audit)
    add_test(NAME ra2.native_crew COMMAND ra2_regression --native-crew-regression ${CMAKE_BINARY_DIR}/native-crew-audit)
    set_tests_properties(ra2.native_crew PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE
        LABELS "assets;rules;combat;weapons;interaction;determinism;persistence")
    add_test(NAME ra2.native_temporal COMMAND ra2_regression --native-temporal-regression ${CMAKE_BINARY_DIR}/native-temporal-audit)
    set_tests_properties(ra2.native_temporal PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE
        LABELS "assets;rules;combat;weapons;interaction;determinism;persistence;render")
    set_tests_properties(ra2.native_ifv PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE
        LABELS "assets;rules;combat;weapons;interaction;determinism;persistence;render")
    set_tests_properties(ra2.native_shrapnel PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE
        LABELS "assets;rules;combat;weapons;determinism;persistence")
    set_tests_properties(ra2.native_weapon_actions PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE
        LABELS "assets;rules;combat;weapons;determinism;persistence;render")
    set_tests_properties(ra2.native_weapons PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;scripting;combat;interaction")
    set_tests_properties(ra2.native_team_attack PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;scripting;combat")
    set_tests_properties(ra2.native_team_orders PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;scripting;interaction;navigation")
    add_test(NAME ra2.native_rockets COMMAND ra2_regression --native-rocket-regression ${CMAKE_BINARY_DIR}/native-rockets-audit)
    add_test(NAME ra2.native_v3 COMMAND ra2_regression --native-v3-regression ${CMAKE_BINARY_DIR}/native-v3-audit)
    add_test(NAME ra2.native_mission COMMAND ra2_regression --native-mission-regression ${CMAKE_BINARY_DIR}/native-mission-audit)
    add_test(NAME ra2.native_campaign COMMAND ra2_regression --native-campaign-regression ${CMAKE_BINARY_DIR}/native-campaign-audit)
    set_tests_properties(ra2.native_campaign PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;campaign;interaction;persistence;screenshot;audio;scripting")
    add_test(NAME ra2.native_demolition COMMAND ra2_regression --native-demolition-regression ${CMAKE_BINARY_DIR}/native-demolition-audit)
    add_test(NAME ra2.native_script_control COMMAND ra2_regression --native-script-control-regression ${CMAKE_BINARY_DIR}/native-script-control-audit)
    add_test(NAME ra2.native_production COMMAND ra2_regression --native-production-regression ${CMAKE_BINARY_DIR}/native-production-audit)
    add_test(NAME ra2.native_ai COMMAND ra2_regression --native-ai-regression ${CMAKE_BINARY_DIR}/native-ai-audit)
    add_test(NAME ra2.native_global_ai COMMAND ra2_regression --native-global-ai-regression ${CMAKE_BINARY_DIR}/native-global-ai-audit)
    add_test(NAME ra2.native_global_app COMMAND ra2_regression --native-global-app-regression ${CMAKE_BINARY_DIR}/native-global-ai-audit)
    add_test(NAME ra2.native_house_leader COMMAND ra2_regression --native-house-leader-regression ${CMAKE_BINARY_DIR}/native-global-ai-audit)
    add_test(NAME ra2.native_nearby_geometry COMMAND ra2_regression --native-nearby-regression)
    add_test(NAME ra2.native_nearby_runtime COMMAND ra2_regression --native-nearby-runtime-regression ${CMAKE_BINARY_DIR}/native-global-ai-audit)
    add_test(NAME ra2.native_source_coordinates COMMAND ra2_regression --native-source-coordinate-regression ${CMAKE_BINARY_DIR}/native-global-ai-audit)
    set_tests_properties(ra2.native_source_coordinates PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE
        FIXTURES_REQUIRED native_global_ai_profiles LABELS "ai;geometry;production;persistence;determinism;transport")
    set_tests_properties(ra2.native_nearby_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 RUN_SERIAL TRUE
        FIXTURES_REQUIRED native_global_ai_profiles LABELS "ai;geometry;production;persistence;determinism;transport")
    set_tests_properties(ra2.native_nearby_geometry PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ai;geometry;determinism;logic")
    set_tests_properties(ra2.native_house_leader PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
        FIXTURES_REQUIRED native_global_ai_profiles LABELS "ai;rules;production;persistence;determinism;transport")
    set_tests_properties(ra2.native_global_app PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
        FIXTURES_REQUIRED native_global_ai_profiles LABELS "ai;application;persistence;determinism;rules")
    set_tests_properties(ra2.native_global_ai PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 SKIP_RETURN_CODE 77
        FIXTURES_REQUIRED native_global_ai_profiles LABELS "ai;production;determinism;rules")
    add_test(NAME ra2.native_crates COMMAND ra2_regression --native-crate-regression ${CMAKE_BINARY_DIR}/native-crates-audit)
    add_test(NAME ra2.native_conditions COMMAND ra2_regression --native-condition-regression ${CMAKE_BINARY_DIR}/native-conditions-audit)
    add_test(NAME ra2.native_playthrough COMMAND ra2_regression --native-playthrough-regression ${CMAKE_BINARY_DIR}/native-playthrough-audit)
    add_test(NAME ra2.native_motion COMMAND ra2_regression --native-motion-regression ${CMAKE_BINARY_DIR}/native-motion-audit)
    add_test(NAME ra2.native_movement COMMAND ra2_regression --native-movement-regression ${CMAKE_BINARY_DIR}/native-movement-audit)
    add_test(NAME ra2.native_flight COMMAND ra2_regression --native-flight-regression ${CMAKE_BINARY_DIR}/native-flight-audit)
    add_test(NAME ra2.native_jumpjet COMMAND ra2_regression --native-jumpjet-regression ${CMAKE_BINARY_DIR}/native-jumpjet-audit)
    add_test(NAME ra2.air_ground COMMAND ra2_regression --air-ground-regression ${CMAKE_BINARY_DIR}/air-ground-audit)
    add_test(NAME ra2.air_production COMMAND ra2_regression --air-production-regression ${CMAKE_BINARY_DIR}/air-production-audit)
    set_tests_properties(ra2.air_production PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 RUN_SERIAL TRUE LABELS "assets;rules;production;movement;determinism;persistence")
    set_tests_properties(ra2.air_ground PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 RUN_SERIAL TRUE LABELS "assets;rules;movement;determinism;persistence;interaction")
    set_tests_properties(ra2.native_jumpjet PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 RUN_SERIAL TRUE LABELS "assets;rules;movement;determinism;persistence;presentation;network")
    set_tests_properties(ra2.native_flight PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;navigation;mechanism")
    set_tests_properties(ra2.native_movement PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;navigation;mechanism")
    add_test(NAME ra2.attack_move COMMAND ra2_regression --attack-move-regression ${CMAKE_BINARY_DIR}/attack-move-audit)
    set_tests_properties(ra2.attack_move PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 RUN_SERIAL TRUE LABELS "determinism;persistence;interaction;navigation;combat")
    set_tests_properties(ra2.native_motion PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;navigation")
    set_tests_properties(ra2.native_playthrough PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 SKIP_RETURN_CODE 77
        LABELS "assets;native;full-match;interaction;scripting;persistence" RUN_SERIAL TRUE)
    set_tests_properties(ra2.native_conditions PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600 SKIP_RETURN_CODE 77
        LABELS "assets;scripting;interaction;production;persistence;determinism" RUN_SERIAL TRUE)
    set_tests_properties(ra2.native_crates PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;scripting;navigation;combat;screenshot")
    set_tests_properties(ra2.native_ai PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;ai;production;scripting;navigation;screenshot")
    set_tests_properties(ra2.native_production PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;production;interaction;screenshot;scripting;navigation")
    set_tests_properties(ra2.native_script_control PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;scripting;combat")
    set_tests_properties(ra2.native_demolition PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;scripting;combat;navigation")
    set_tests_properties(ra2.native_mission PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;scripting;combat")
    set_tests_properties(ra2.native_v3 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;combat;interaction")
    set_tests_properties(ra2.native_rockets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;combat")
    set_tests_properties(ra2.native_teams PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 900
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;scripting;combat")
    set_tests_properties(ra2.native_events PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;interaction;scripting")
    set_tests_properties(ra2.native_gates PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;interaction;navigation")
    set_tests_properties(ra2.native_bridge_repair PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;interaction;navigation")
    set_tests_properties(ra2.native_bridge PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "assets;determinism;persistence;screenshot;navigation")
    set_tests_properties(ra2.native_scene PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 150
        SKIP_RETURN_CODE 77 LABELS "assets;determinism;persistence;screenshot")
    set_tests_properties(ra2.native_map PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120
        SKIP_RETURN_CODE 77 LABELS "assets;determinism;persistence;screenshot")
    set_tests_properties(ra2.native_audio PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 LABELS "assets;audio;presentation;interaction;determinism")
    set_tests_properties(ra2.unit_voices PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 LABELS "assets;presentation;interaction;determinism;audio")
    add_test(NAME ra2.campaign_save COMMAND ra2_regression --campaign-save-regression --mod ${CMAKE_SOURCE_DIR}/tests/fixtures/campaign-save)
    set_tests_properties(ra2.campaign_save PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 LABELS "campaign;determinism;persistence;scripting")
    add_test(NAME ra2.net_protocol COMMAND ra2_regression --net-protocol-regression)
    set_tests_properties(ra2.net_protocol PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 LABELS "network;determinism;interaction")
    add_test(NAME ra2.net_production COMMAND ra2_regression --net-production-regression ${CMAKE_BINARY_DIR}/net-production-audit)
    set_tests_properties(ra2.net_production PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 RUN_SERIAL TRUE LABELS "network;production;interaction;rules;determinism")
    add_test(NAME ra2.modal_input COMMAND ra2_regression --modal-input-regression ${CMAKE_BINARY_DIR}/modal-input-audit)
    set_tests_properties(ra2.modal_input PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 RUN_SERIAL TRUE LABELS "interaction;presentation;rules;network;determinism")
    add_test(NAME ra2.classic_input COMMAND ra2_regression --classic-input-regression ${CMAKE_BINARY_DIR}/classic-input-audit)
    set_tests_properties(ra2.classic_input PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180 RUN_SERIAL TRUE LABELS "interaction;rules;network;presentation")
    add_test(NAME ra2.original_skirmish COMMAND ra2_regression --original-skirmish-regression ${CMAKE_BINARY_DIR}/original-skirmish-audit)
    set_tests_properties(ra2.original_skirmish PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 600 RUN_SERIAL TRUE LABELS "interaction;rules;map;natural-start;determinism;persistence")
    foreach(original_map RANGE 0 1)
        add_test(NAME ra2.original_skirmish_natural_${original_map} COMMAND ra2_regression --original-skirmish-natural ${CMAKE_BINARY_DIR}/original-skirmish-natural-audit ${original_map})
        set_tests_properties(ra2.original_skirmish_natural_${original_map} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 900 RUN_SERIAL TRUE LABELS "map;natural-start;outcome;AI;determinism;persistence")
    endforeach()
    set_tests_properties(ra2.app_state PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120 LABELS "determinism;persistence" RUN_SERIAL TRUE)
    add_test(NAME ra2.state COMMAND ra2_regression --state-test)
    set_tests_properties(ra2.state PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 60
    )
    # 可失败的遭遇战 smoke / 32 关战役矩阵（期望不符返回非零）
    add_test(NAME ra2.smoke COMMAND ra2_regression --smoke)
    set_tests_properties(ra2.smoke PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 300
    )
    add_test(NAME ra2.campaign_matrix COMMAND ra2_regression --campaign-matrix)
    set_tests_properties(ra2.campaign_matrix PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 600
        SKIP_RETURN_CODE 77
    )
    set_tests_properties(ra2.logic ra2.state PROPERTIES LABELS "mechanism;determinism")
    set_tests_properties(ra2.smoke PROPERTIES LABELS "mechanism;screenshot")
    set_tests_properties(ra2.campaign_matrix PROPERTIES LABELS "campaign;mechanism")
    if(WIN32)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME ra2.verification_runner COMMAND ${Python3_EXECUTABLE} -X utf8 -B -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tests -p verify_runner_tests.py)
        set_tests_properties(ra2.verification_runner PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30 LABELS "tools;determinism")
        add_test(NAME ra2.native_weapon_catalog_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_weapon_catalog_assets_test.py)
        add_test(NAME ra2.native_crew_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_crew_assets_test.py)
        set_tests_properties(ra2.native_crew_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "assets;rules;weapons")
        add_test(NAME ra2.native_ifv_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_ifv_assets_test.py)
        set_tests_properties(ra2.native_ifv_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "assets;rules;weapons;presentation")
        set_tests_properties(ra2.native_weapon_catalog_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "assets;rules;weapons")
        add_test(NAME ra2.native_scene_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_scene_assets_test.py)
        add_test(NAME ra2.native_scenery_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_scenery_assets_test.py)
        add_test(NAME ra2.native_events_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_events_assets_test.py)
        add_test(NAME ra2.native_campaign_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_campaign_assets_test.py)
        add_test(NAME ra2.source_rules COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/source_rules_test.py)
        add_test(NAME ra2.source_ai COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_SOURCE_DIR}/tests/source_ai_test.py)
        foreach(ai_edition ra2 yr)
            add_test(NAME ra2.source_ai_runtime_${ai_edition} COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_SOURCE_DIR}/tools/ra2pack/source_ai.py ${ai_edition}
                --profile ${CMAKE_SOURCE_DIR}/assets/rules/profiles/${ai_edition}.ini
                --output ${CMAKE_BINARY_DIR}/native-global-ai-audit/${ai_edition}-profile.ini --runtime-profile)
            set_tests_properties(ra2.source_ai_runtime_${ai_edition} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60
                FIXTURES_SETUP native_global_ai_profiles LABELS "assets;ai;rules")
        endforeach()
        set_tests_properties(ra2.source_ai PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "assets;format;rules;ai")
        set_tests_properties(ra2.source_rules PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180
            SKIP_RETURN_CODE 77 LABELS "assets;format;rules;campaign")
        set_tests_properties(ra2.native_campaign_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30
            SKIP_RETURN_CODE 77 LABELS "assets;format;campaign;scripting")
        add_test(NAME ra2.native_demolition_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_demolition_assets_test.py)
        add_test(NAME ra2.native_production_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_production_assets_test.py)
        add_test(NAME ra2.native_ai_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_ai_assets_test.py)
        add_test(NAME ra2.native_crates_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_crates_assets_test.py)
        add_test(NAME ra2.native_conditions_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_conditions_assets_test.py)
        add_test(NAME ra2.native_motion_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_motion_assets_test.py)
        add_test(NAME ra2.native_movement_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_movement_assets_test.py)
        set_tests_properties(ra2.native_movement_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format")
        set_tests_properties(ra2.native_motion_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format")
        set_tests_properties(ra2.native_conditions_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format;scripting")
        set_tests_properties(ra2.native_crates_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format;scripting")
        set_tests_properties(ra2.native_ai_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format;ai;production")
        set_tests_properties(ra2.native_production_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format;production")
        set_tests_properties(ra2.native_demolition_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format;scripting;combat")
        add_test(NAME ra2.native_rockets_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_rockets_assets_test.py)
        add_test(NAME ra2.native_targets_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_targets_assets_test.py)
        add_test(NAME ra2.native_weapons_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_weapons_assets_test.py)
        set_tests_properties(ra2.native_weapons_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format;combat")
        set_tests_properties(ra2.native_targets_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format;scripting;combat")
        set_tests_properties(ra2.native_rockets_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "assets;format;combat")
        set_tests_properties(ra2.native_events_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "assets;format;scripting")
        set_tests_properties(ra2.native_scenery_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "assets;format")
        set_tests_properties(ra2.native_scene_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "assets;format")
        add_test(NAME ra2.native_map_format COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_map_format_test.py)
        set_tests_properties(ra2.native_map_format PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "assets;format")
        add_test(NAME ra2.native_map_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_map_assets_test.py)
        set_tests_properties(ra2.native_map_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "assets;format")
        add_test(NAME ra2.native_audio_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_audio_assets_test.py)
        set_tests_properties(ra2.native_audio_assets PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30 LABELS "assets;format;audio")
        add_test(NAME ra2.unit_voice_codec COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/unit_voice_codec_test.py
            --output ${CMAKE_BINARY_DIR}/unit-voice-codec)
        set_tests_properties(ra2.unit_voice_codec PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "assets;format;audio")
        add_test(NAME ra2.animation_matrix COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/animation_matrix.py
            --exe $<TARGET_FILE:ra2_regression> --output ${CMAKE_BINARY_DIR}/animation-audit)
        set_tests_properties(ra2.animation_matrix PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;presentation;determinism")
        add_test(NAME ra2.chrono_pipeline COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/chrono_fx_pipeline_test.py)
        set_tests_properties(ra2.chrono_pipeline PROPERTIES TIMEOUT 15 LABELS "assets;format;presentation")
        add_test(NAME ra2.chrono_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/chrono_asset_matrix.py --exe $<TARGET_FILE:ra2_regression>)
        set_tests_properties(ra2.chrono_assets PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;presentation;validation")
        add_test(NAME ra2.asset_pipeline COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/asset_pipeline_test.py)
        add_test(NAME ra2.voxel_projection COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/voxel_projection_matrix.py --exe $<TARGET_FILE:ra2_regression> --output ${CMAKE_BINARY_DIR}/voxel-projection-audit)
        add_test(NAME ra2.chopper_pipeline COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/chopper_deploy_pipeline_test.py)
        set_tests_properties(ra2.chopper_pipeline PROPERTIES TIMEOUT 15 LABELS "assets;format;presentation")
        set_tests_properties(ra2.voxel_projection PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;format;presentation;validation")
        set_tests_properties(ra2.asset_pipeline PROPERTIES TIMEOUT 15 LABELS "assets;format")
        add_test(NAME ra2.tmp_decode COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/tmp_decode_test.py)
        set_tests_properties(ra2.tmp_decode PROPERTIES TIMEOUT 15 LABELS "assets;format")
        add_test(NAME ra2.cameo_pipeline COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/cameo_pipeline_test.py)
        set_tests_properties(ra2.cameo_pipeline PROPERTIES TIMEOUT 15 LABELS "assets;presentation")
        add_test(NAME ra2.hud_matrix COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/hud_matrix.py
            --exe $<TARGET_FILE:ra2_regression> --output ${CMAKE_BINARY_DIR}/hud-audit)
        set_tests_properties(ra2.hud_matrix PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "presentation;interaction;screenshot")
        add_test(NAME ra2.visual_matrix COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/visual_matrix.py
            --exe $<TARGET_FILE:ra2_regression> --output ${CMAKE_BINARY_DIR}/visual-audit)
        set_tests_properties(ra2.visual_matrix PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120 RUN_SERIAL TRUE LABELS "presentation;interaction;performance")
        add_test(NAME ra2.presentation_matrix
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/presentation_matrix.py --exe $<TARGET_FILE:ra2_regression>)
        set_tests_properties(ra2.presentation_matrix PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "determinism;presentation")
        add_test(
            NAME ra2.net_dual_process
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tests/net_dual_process.ps1
                    -Exe $<TARGET_FILE:ra2_regression> -Frames 360
        )
        set_tests_properties(ra2.net_dual_process PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
            RUN_SERIAL TRUE
        )
        add_test(NAME ra2.net_rules
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tests/net_rules_compatibility.ps1
                    -Exe $<TARGET_FILE:ra2_regression> -OutputDir ${CMAKE_BINARY_DIR}/net-rules-audit)
        set_tests_properties(ra2.net_rules PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 600 RUN_SERIAL TRUE LABELS "network;rules;determinism;interaction")
        add_test(NAME ra2.net_profiles
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tests/net_rules_compatibility.ps1
                    -Exe $<TARGET_FILE:ra2_regression> -OutputDir ${CMAKE_BINARY_DIR}/net-profiles-audit -ProfilesOnly)
        set_tests_properties(ra2.net_profiles PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 600 RUN_SERIAL TRUE LABELS "network;rules;determinism;interaction;assets")
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
        # Instrumented movement and twelve-match matrices exceed Release
        # wall-clock budgets. Simulation windows and outcome assertions stay
        # unchanged; only the MSVC ASan process waiting limit differs.
        set_tests_properties(ra2.movement ra2.match_completion PROPERTIES TIMEOUT 600)
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
