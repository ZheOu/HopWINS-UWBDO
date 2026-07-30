cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)
if(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "CMAKE_BUILD_TYPE must be Debug or Release")
endif()

set(CMAKE_PROJECT_NAME HopWINS-UWBDO-MCU)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)

add_executable(${CMAKE_PROJECT_NAME})
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    "-Wl,-Map=${CMAKE_PROJECT_NAME}.map"
)
set_property(
    TARGET ${CMAKE_PROJECT_NAME}
    APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/STM32U585xx_FLASH.ld"
)

message(STATUS
    "Firmware: ${CMAKE_PROJECT_NAME}, build=${CMAKE_BUILD_TYPE}, "
    "toolchain=${CMAKE_C_COMPILER_ID}")

# The full 128 KiB FPGA region is reserved by STM32U585xx_FLASH.ld.
set(FPGA_IMAGE_BIN
    "${CMAKE_CURRENT_SOURCE_DIR}/../FPGA/impl_1/HopWINS_UWBDO_FPGA_impl_1.bin")
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
    "FPGA image: ${FPGA_IMAGE_SIZE}/${FPGA_IMAGE_MAX_SIZE} bytes")

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

add_subdirectory(cmake/stm32cubemx)
add_subdirectory(Services)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Boards/HopWINS-UWBDO-PCB-STDWDO/board.c
    Drivers/Components/CLK/SiT3907ACP2F-25NZ-38.400000/sit3907.c
    Drivers/Components/FPGA/ICE40UP5K-SG48I/ice40.c
    Drivers/Components/UWB/DW3220TR13/deca_compat.c
    Drivers/Components/UWB/DW3220TR13/deca_interface.c
    Drivers/Components/UWB/DW3220TR13/deca_rsl.c
    Drivers/Components/UWB/DW3220TR13/dw3000.c
    Drivers/Components/UWB/DW3220TR13/dw3000_device.c
    "${FPGA_IMAGE_OBJECT}"
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Boards/HopWINS-UWBDO-PCB-STDWDO
    Drivers/Components/CLK/SiT3907ACP2F-25NZ-38.400000
    Drivers/Components/FPGA/ICE40UP5K-SG48I
    Drivers/Components/UWB/DW3220TR13
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_DRV_DW3000
    HOPWINS_BUILD_TYPE_NAME="${CMAKE_BUILD_TYPE}"
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)
