cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Version information
set(FIRMWARE_VERSION "5" CACHE STRING "Firmware version number (unsigned integer)")
set(HARDWARE_VERSION "3" CACHE STRING "Hardware version number (unsigned integer)")

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type selected, defaulting to Debug")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME usb-lin-card-fw)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Enable some extra warnings (adjust stack usage here if needed)
add_compile_options(
    -Wstack-usage=256
    -Wvla
)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Add minlin library
set(minlin_HARDENING OFF)
set(minlin_HARDENING_GLOBALLY OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/minlin)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    src/app.c
    src/board_stm32.c
    src/lin_monitor.c
    src/slcan.c
    src/stm32_minlin_hw.c
    mcutils/stm32/stack_monitor.c
    mcutils/stm32/fast_ring_buffer.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    minlin
    mcutils/stm32
    src
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    $<$<CONFIG:Debug>:DEBUG>
    FIRMWARE_VERSION=${FIRMWARE_VERSION}
    HARDWARE_VERSION=${HARDWARE_VERSION}
)

# Enable bunch of good practice compiler warnings and other quality checking tools
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wall                   # Warnings for constructs often associated with defects.
    -Wextra                 # -"-
    -Wformat                # Additional format function warnings.
    -Wformat=2              # -"-
    -Wconversion            # Implicit conversions that may lose data.
    -Wimplicit-fallthrough  # A switch case falls through.
    -Werror=format-security # Treat non-literal format strings as errors.
    -Wshadow                # A local variable shadows another.
    -Wcast-align            # Cast increases alignment requirement.
    -Wunused                # Unused variables, functions, etc.
    -Wsign-conversion       # Sign conversions.
    -Wnull-dereference      # Possible null pointer dereference.
    -Wdouble-promotion      # Implicit conversion from float to double.
    -Werror=implicit                    # Implicit function declarations.
    -Werror=incompatible-pointer-types  # Pointer type mismatches.
    -Werror=int-conversion              # Implicit int conversions that may change value.
    #-fanalyzer              # Enable static analysis
)

# Some STM32CubeMX generated code cause warnings. Disable those avoid polluting the output.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/App/usbd_desc.c
    PROPERTIES COMPILE_FLAGS "-Wno-conversion"
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/Target/usbd_conf.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter"
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
    minlin::minlin
)
