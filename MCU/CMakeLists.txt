cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME HopWINS-UWBDO)

# The cross-compilation toolchain is selected by CMakePresets.json.

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
set_property(
    TARGET ${CMAKE_PROJECT_NAME}
    APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/STM32U585xx_FLASH.ld"
)

# Embed the Lattice configuration image in the STM32 internal Flash image.
set(FPGA_IMAGE_BIN
    "${CMAKE_CURRENT_SOURCE_DIR}/../FPGA/impl_1/test_impl_1.bin"
    CACHE FILEPATH "Path to the iCE40 configuration binary")
set(FPGA_IMAGE_MAX_SIZE 131072)
set(FPGA_IMAGE_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/fpga_image.o")

if(NOT EXISTS "${FPGA_IMAGE_BIN}")
    message(FATAL_ERROR "FPGA image not found: ${FPGA_IMAGE_BIN}")
endif()

file(SIZE "${FPGA_IMAGE_BIN}" FPGA_IMAGE_SIZE)
if(FPGA_IMAGE_SIZE GREATER FPGA_IMAGE_MAX_SIZE)
    message(FATAL_ERROR
        "FPGA image is ${FPGA_IMAGE_SIZE} bytes; reserved Flash capacity is "
        "${FPGA_IMAGE_MAX_SIZE} bytes")
endif()

message(STATUS
    "FPGA image: ${FPGA_IMAGE_BIN} (${FPGA_IMAGE_SIZE}/${FPGA_IMAGE_MAX_SIZE} bytes)")

add_custom_command(
    OUTPUT "${FPGA_IMAGE_OBJECT}"
    COMMAND "${CMAKE_OBJCOPY}"
        -I binary
        -O elf32-littlearm
        -B arm
        --rename-section
        .data=.fpga_image,alloc,load,readonly,data,contents
        "${FPGA_IMAGE_BIN}"
        "${FPGA_IMAGE_OBJECT}"
    DEPENDS "${FPGA_IMAGE_BIN}"
    COMMENT "Embedding iCE40 configuration image"
    VERBATIM
)

set_source_files_properties(
    "${FPGA_IMAGE_OBJECT}"
    PROPERTIES
        GENERATED TRUE
        EXTERNAL_OBJECT TRUE
)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Boards/HopWINS-UWBDO-PCB-STDWDO/board.c
    Drivers/Components/FPGA/ICE40/ice40.c
    Drivers/Components/UWB/DW3000/dw3000.c
    Drivers/Components/UWB/DW3000/deca_compat.c
    Drivers/Components/UWB/DW3000/deca_interface.c
    Drivers/Components/UWB/DW3000/deca_rsl.c
    Drivers/Components/UWB/DW3000/dw3000_device.c
    "${FPGA_IMAGE_OBJECT}"
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Boards/HopWINS-UWBDO-PCB-STDWDO
    Drivers/Components/FPGA/ICE40
    Drivers/Components/UWB/DW3000
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    USE_DRV_DW3000
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
