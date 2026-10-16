cmake_minimum_required(VERSION 3.15)
project(elib_state_machine C)

# --- Options -------------------------------------------------------------
option(ELIB_BUILD_TESTS "Build elib-state-machine test suites" ON)
set(ELIB_FSM_HSM_MAX_DEPTH "" CACHE STRING
    "Maximum HSM hierarchy depth (empty = library default of 16)")

# --- Language / warnings -------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra)
endif()

# --- Libraries -----------------------------------------------------------
# One static library per state-machine flavour.
add_library(elib_fsm           STATIC src/elib_fsm_core.c)
add_library(elib_fsm_cb        STATIC src/elib_fsm_cb_core.c)
add_library(elib_fsm_hsm       STATIC src/elib_fsm_hsm_core.c)
add_library(elib_fsm_hsm_hist  STATIC src/elib_fsm_hsm_hist_core.c)

set(ELIB_LIBS elib_fsm elib_fsm_cb elib_fsm_hsm elib_fsm_hsm_hist)
foreach(lib IN LISTS ELIB_LIBS)
    target_include_directories(${lib} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endforeach()

# Optional: override the maximum hierarchy depth (same as -DELIB_FSM_HSM_MAX_DEPTH).
if(ELIB_FSM_HSM_MAX_DEPTH)
    foreach(lib elib_fsm_hsm elib_fsm_hsm_hist)
        target_compile_definitions(${lib} PRIVATE
            ELIB_FSM_HSM_MAX_DEPTH=${ELIB_FSM_HSM_MAX_DEPTH})
    endforeach()
endif()

# --- Tests ---------------------------------------------------------------
if(ELIB_BUILD_TESTS)
    enable_testing()

    # Tests rely on assert(), so never let NDEBUG strip them (even in Release).
    function(elib_add_test name test_src lib)
        add_executable(${name} ${test_src})
        target_link_libraries(${name} PRIVATE ${lib})
        if(MSVC)
            target_compile_options(${name} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${name} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    elib_add_test(test_elib_fsm          test/test_elib_fsm.c          elib_fsm)
    elib_add_test(test_elib_fsm_cb       test/test_elib_fsm_cb.c       elib_fsm_cb)
    elib_add_test(test_elib_fsm_hsm      test/test_elib_fsm_hsm.c      elib_fsm_hsm)
    elib_add_test(test_elib_fsm_hsm_hist test/test_elib_fsm_hsm_hist.c elib_fsm_hsm_hist)
endif()
