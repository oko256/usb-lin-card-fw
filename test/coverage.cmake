# Code coverage reporting for the host unit tests with gcov, fastcov and genhtml.
#
# Include this before any targets are defined. With COVERAGE=ON, everything is compiled with
# --coverage, and coverage_add_target() adds a target that runs all tests (ctest) and generates an
# HTML report into <build dir>/coverage/index.html (plus coverage.info in lcov format).

option(COVERAGE "Compile with coverage instrumentation and add the 'coverage' target" OFF)

if(COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "COVERAGE requires GCC (gcov), got ${CMAKE_C_COMPILER_ID}")
    endif()
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# coverage_add_target(INCLUDE <dirs/files...> [EXCLUDE <dirs/files...>])
#
# Adds a 'coverage' target (if COVERAGE is enabled) that zeroes the counters, runs all tests and
# generates the report. Only source files whose path contains one of the INCLUDE strings and none
# of the EXCLUDE strings are reported. Relative paths are taken relative to the current source
# directory.
function(coverage_add_target)
    if(NOT COVERAGE)
        return()
    endif()
    cmake_parse_arguments(arg "" "" "INCLUDE;EXCLUDE" ${ARGN})

    # gcov reports paths as given to the compiler: normalize them the same way as the sources
    # (which must be added with normalized paths too, i.e. no "..").
    foreach(i_list INCLUDE EXCLUDE)
        set(_${i_list} "")
        foreach(i_path ${arg_${i_list}})
            cmake_path(ABSOLUTE_PATH i_path BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} NORMALIZE)
            list(APPEND _${i_list} ${i_path})
        endforeach()
    endforeach()
    if(NOT _INCLUDE)
        message(FATAL_ERROR "coverage_add_target: INCLUDE is required")
    endif()

    find_program(FASTCOV_PROGRAM NAMES fastcov fastcov.py REQUIRED)
    find_program(GCOV_PROGRAM NAMES gcov REQUIRED)
    find_program(GENHTML_PROGRAM NAMES genhtml REQUIRED)

    set(_fastcov ${FASTCOV_PROGRAM} --branch-coverage --gcov ${GCOV_PROGRAM}
        --search-directory ${PROJECT_BINARY_DIR})
    if(_EXCLUDE)
        set(_exclude_args --exclude ${_EXCLUDE})
    endif()

    add_custom_target(coverage
        COMMAND ${_fastcov} --zerocounters
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND ${_fastcov} --process-gcno --include ${_INCLUDE} ${_exclude_args}
            --lcov --output coverage.info
        COMMAND ${GENHTML_PROGRAM} --branch-coverage --ignore-errors inconsistent,unsupported
            --output-directory coverage coverage.info
        COMMAND ${CMAKE_COMMAND} -E echo
            "Coverage report: file://${PROJECT_BINARY_DIR}/coverage/index.html"
        BYPRODUCTS coverage.info coverage/index.html
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        VERBATIM
        COMMENT "Running tests and generating coverage report"
    )
    # Build all test executables (of this directory) first.
    get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(i_target ${_targets})
        get_target_property(_type ${i_target} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            add_dependencies(coverage ${i_target})
        endif()
    endforeach()
endfunction()
