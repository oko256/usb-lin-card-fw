cmake_minimum_required(VERSION 3.28)

# Host unit tests for the firmware application logic. Built with the host compiler, separately
# from the firmware itself (which is cross-compiled from the parent directory).
project(test_usb-lin-card-fw LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/coverage.cmake)

# Sanitizers for everything, including minlin.
set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
add_compile_options(${SANITIZER_FLAGS})
add_link_options(${SANITIZER_FLAGS})

# Fetch Unity unit testing framework
include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY
    https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.7.0
)
set(FETCHCONTENT_UPDATES_DISCONNECTED_unity ON)
FetchContent_MakeAvailable(unity)

# Build the real minlin library for the host.
set(minlin_HARDENING OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../minlin ${CMAKE_CURRENT_BINARY_DIR}/minlin)

cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH FW_DIR)
set(SRC ${FW_DIR}/src)
set(MCUTILS_SRC ${FW_DIR}/mcutils/stm32)

enable_testing()

# Adds a test executable from the given sources.
function(add_fw_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs
        ${SRC}
        ${MCUTILS_SRC}
    )
    target_compile_definitions(${name} PRIVATE
        FIRMWARE_VERSION=1
        HARDWARE_VERSION=3
    )
    target_compile_options(${name} PRIVATE
        -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Werror
    )
    target_link_libraries(${name} PRIVATE unity::framework minlin::minlin)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_fw_test(test_lin_monitor
    lin_monitor.test.c
    ${SRC}/lin_monitor.c
    stubs/minlin_hw_stub.c
)

add_fw_test(test_slcan
    slcan.test.c
    ${SRC}/slcan.c
    ${MCUTILS_SRC}/fast_ring_buffer.c
    stubs/board_stub.c
)

add_fw_test(test_app
    app.test.c
    ${SRC}/app.c
    ${SRC}/slcan.c
    ${SRC}/lin_monitor.c
    ${MCUTILS_SRC}/fast_ring_buffer.c
    stubs/board_stub.c
    stubs/minlin_hw_stub.c
)

# Only the firmware's own sources: minlin and mcutils have their own tests and reports.
coverage_add_target(INCLUDE ${SRC})
