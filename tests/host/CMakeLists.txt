cmake_minimum_required(VERSION 3.13)
project(flare_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(FIRMWARE_SRC ${REPO_ROOT}/firmware/src)
set(FIRMWARE_INCLUDE ${REPO_ROOT}/firmware/include)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# tune.h is generated from config.ini the same way firmware/CMakeLists.txt
# generates it — this is a standalone project so it cannot rely on that build
# having already run.
set(GEN_CONFIG_SCRIPT ${REPO_ROOT}/scripts/gen_config.py)
set(USER_CONFIG_INI ${REPO_ROOT}/config.ini)
set(TUNE_H ${FIRMWARE_INCLUDE}/tune.h)

add_custom_command(
  OUTPUT ${TUNE_H}
  COMMAND ${Python3_EXECUTABLE} ${GEN_CONFIG_SCRIPT} ${USER_CONFIG_INI} ${TUNE_H}
  DEPENDS ${USER_CONFIG_INI} ${GEN_CONFIG_SCRIPT}
  COMMENT "Generating tune.h from config.ini"
)

# sim_globals.c is generated from the g_ definitions in firmware/src/main.c —
# never hand-edited, regenerated whenever main.c changes. See design.md
# "Globals: generated, not hand-written".
set(GEN_GLOBALS_SCRIPT ${REPO_ROOT}/scripts/gen_sim_globals.py)
set(MAIN_C ${FIRMWARE_SRC}/main.c)
set(SIM_GLOBALS_C ${CMAKE_CURRENT_BINARY_DIR}/generated/sim_globals.c)

add_custom_command(
  OUTPUT ${SIM_GLOBALS_C}
  COMMAND ${Python3_EXECUTABLE} ${GEN_GLOBALS_SCRIPT} ${MAIN_C} ${SIM_GLOBALS_C}
  DEPENDS ${MAIN_C} ${GEN_GLOBALS_SCRIPT}
  COMMENT "Generating sim_globals.c from firmware/src/main.c"
)

add_executable(flare_sim
  ${FIRMWARE_SRC}/sync.c
  ${FIRMWARE_SRC}/sync_buf.c
  ${FIRMWARE_SRC}/sync_relay.c
  ${FIRMWARE_SRC}/sync_analog.c
  ${FIRMWARE_SRC}/motion.c
  ${FIRMWARE_SRC}/toolchange.c
  ${FIRMWARE_SRC}/cutter.c
  ${FIRMWARE_SRC}/settings_store.c
  ${SIM_GLOBALS_C}
  sim_fakes.c
  sim_plant.c
  sim_scenario.c
  sim_trace.c
  sim_main.c
)

# Shim include dir MUST precede firmware/include so pico/*.h and hardware/*.h
# resolve to the host shims, not the (absent) Pico SDK.
target_include_directories(flare_sim PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/shims
  ${FIRMWARE_INCLUDE}
  ${CMAKE_CURRENT_LIST_DIR}
)

# -Wall -Wextra scoped to tests/host/'s own sources only. firmware/src/*.c is
# compiled unmodified and warning-clean under its own build (firmware/
# CMakeLists.txt), but was not written against -Wextra specifically — applying
# it here would surface pre-existing firmware style debt (unused-parameter,
# sign-compare) with no in-scope fix (zero firmware edits, per proposal.md
# Scope). The "no warnings" accept criterion is about the new sim code.
set(SIM_OWN_SOURCES
  sim_fakes.c sim_plant.c sim_scenario.c sim_trace.c sim_main.c
  ${SIM_GLOBALS_C}
)
set_source_files_properties(${SIM_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
