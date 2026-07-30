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

include(Boards/HopWINS-UWBDO-PCB-STDWDO/board_target.cmake)

set(
    HOPWINS_APP_ROLE
    "DO_FOLLOWER"
    CACHE STRING
    "Top-level firmware role"
)
set_property(
    CACHE HOPWINS_APP_ROLE
    PROPERTY STRINGS DO_LEADER DO_FOLLOWER
)

if(HOPWINS_APP_ROLE STREQUAL "DO_LEADER")
    set(HOPWINS_APP_ROLE_DEFINE HOPWINS_APP_ROLE_DO_LEADER)
    set(HOPWINS_FIRMWARE_OUTPUT_NAME "HopWINS-DO-Leader")
elseif(HOPWINS_APP_ROLE STREQUAL "DO_FOLLOWER")
    set(HOPWINS_APP_ROLE_DEFINE HOPWINS_APP_ROLE_DO_FOLLOWER)
    set(HOPWINS_FIRMWARE_OUTPUT_NAME "HopWINS-DO-Follower")
    if(NOT HOPWINS_BOARD_EMBED_FPGA_IMAGE OR
       NOT HOPWINS_BOARD_HAS_CLOCK_CONTROL)
        message(FATAL_ERROR
            "DO_FOLLOWER requires a PCB target with FPGA and clock control")
    endif()
else()
    message(FATAL_ERROR
        "Unsupported HOPWINS_APP_ROLE: ${HOPWINS_APP_ROLE}")
endif()

set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES OUTPUT_NAME "${HOPWINS_FIRMWARE_OUTPUT_NAME}"
)
set_property(
    TARGET ${CMAKE_PROJECT_NAME}
    APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/STM32U585xx_FLASH.ld"
)

message(STATUS
    "Firmware: board=${HOPWINS_BOARD_TARGET}, role=${HOPWINS_APP_ROLE}, "
    "build=${CMAKE_BUILD_TYPE}")

if(HOPWINS_BOARD_EMBED_FPGA_IMAGE)
    # Embed the Lattice configuration image in the STM32 internal Flash image.
    set(FPGA_IMAGE_BIN
        "${CMAKE_CURRENT_SOURCE_DIR}/../FPGA/impl_1/HopWINS_UWBDO_FPGA_impl_1.bin"
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
else()
    message(STATUS "PCB assembly: FPGA not populated; image is not embedded")
endif()

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(Services)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Boards/HopWINS-UWBDO-PCB-STDWDO/board.c
    Drivers/Components/UWB/DW3000/dw3000.c
    Drivers/Components/UWB/DW3000/deca_compat.c
    Drivers/Components/UWB/DW3000/deca_interface.c
    Drivers/Components/UWB/DW3000/deca_rsl.c
    Drivers/Components/UWB/DW3000/dw3000_device.c
)

if(HOPWINS_BOARD_EMBED_FPGA_IMAGE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        Drivers/Components/FPGA/ICE40/ice40.c
        "${FPGA_IMAGE_OBJECT}"
    )
endif()

# The DCXO is only fitted on assemblies that carry clock control, so the driver
# follows the same population gate the board target already publishes.
if(HOPWINS_BOARD_HAS_CLOCK_CONTROL)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        Drivers/Components/CLK/SiT3907ACP2F-25NZ-38.400000/sit3907.c
    )
endif()

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Boards/HopWINS-UWBDO-PCB-STDWDO
    Drivers/Components/CLK/SiT3907ACP2F-25NZ-38.400000
    Drivers/Components/FPGA/ICE40
    Drivers/Components/UWB/DW3000
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    USE_DRV_DW3000
    HOPWINS_BOARD_TARGET=${HOPWINS_BOARD_TARGET_DEFINE}
    HOPWINS_APP_ROLE=${HOPWINS_APP_ROLE_DEFINE}
    HOPWINS_BUILD_TYPE_NAME="${CMAKE_BUILD_TYPE}"
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
