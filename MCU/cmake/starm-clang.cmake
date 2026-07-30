set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(STARM_CLANG NAMES starm-clang REQUIRED)
find_program(STARM_OBJCOPY NAMES starm-objcopy REQUIRED)
find_program(STARM_SIZE NAMES starm-size REQUIRED)

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    STARM_CLANG
    STARM_OBJCOPY
    STARM_SIZE
)

get_filename_component(STARM_BIN_DIR "${STARM_CLANG}" DIRECTORY)
get_filename_component(STARM_TOOLCHAIN_DIR "${STARM_BIN_DIR}" DIRECTORY)
execute_process(
    COMMAND "${STARM_CLANG}" --print-resource-dir
    OUTPUT_VARIABLE STARM_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${STARM_CLANG}"
        --target=arm-st-none-eabi
        -mcpu=cortex-m33
        -mfloat-abi=hard
        -mthumb
        --config=newlib.cfg
        -print-multi-directory
    OUTPUT_VARIABLE STARM_MULTILIB_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
set(STARM_NEWLIB_DIR
    "${STARM_TOOLCHAIN_DIR}/lib/clang-runtimes/newlib")

set(CMAKE_C_COMPILER "${STARM_CLANG}")
set(CMAKE_ASM_COMPILER "${STARM_CLANG}")
set(CMAKE_OBJCOPY "${STARM_OBJCOPY}")
set(CMAKE_SIZE "${STARM_SIZE}")

set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")

set(STARM_CPU_FLAGS
    "--target=arm-st-none-eabi -mcpu=cortex-m33 -mfloat-abi=hard -mthumb --config=newlib.cfg")
set(STARM_SYSTEM_INCLUDE_FLAGS
    "-isystem \"${STARM_RESOURCE_DIR}/include\" \
    -isystem \"${STARM_NEWLIB_DIR}/${STARM_MULTILIB_DIR}/include\" \
    -isystem \"${STARM_NEWLIB_DIR}/arm-none-eabi/include\"")
set(STARM_COMMON_FLAGS
    "${STARM_CPU_FLAGS} ${STARM_SYSTEM_INCLUDE_FLAGS} \
    -Wall -Wextra -Wpedantic -fdata-sections -ffunction-sections")

set(CMAKE_C_FLAGS_INIT "${STARM_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT
    "${STARM_CPU_FLAGS} -x assembler-with-cpp -Wno-unused-command-line-argument")
set(CMAKE_C_FLAGS_DEBUG_INIT "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE_INIT "-Oz -g0")
set(CMAKE_ASM_FLAGS_DEBUG_INIT "-g3")
set(CMAKE_ASM_FLAGS_RELEASE_INIT "-g0")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${STARM_CPU_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32U585xx_FLASH.ld\" \
    -Wl,--gc-sections \
    -Wl,--print-memory-usage \
    -Wl,-z,max-page-size=8 \
    -Wl,-z,noexecstack")
