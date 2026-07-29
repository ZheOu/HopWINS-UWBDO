option(
    HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
    "Run the temporary DW3000 reference-clock diagnostic during startup"
    OFF
)

if(HOPWINS_APP_ROLE STREQUAL "DO_LEADER")
    set(HOPWINS_ROLE_SOURCE Roles/do_leader_service.c)
elseif(HOPWINS_APP_ROLE STREQUAL "DO_FOLLOWER")
    set(HOPWINS_ROLE_SOURCE Roles/do_follower_service.c)
else()
    message(FATAL_ERROR
        "Services do not support HOPWINS_APP_ROLE=${HOPWINS_APP_ROLE}")
endif()

set(
    HOPWINS_UWB_CIR_SAMPLE_COUNT
    0
    CACHE STRING
    "CIR samples per capture; 0 exports the complete accumulator"
)
set(
    HOPWINS_UWB_CIR_PRE_FIRST_PATH_SAMPLES
    64
    CACHE STRING
    "Samples retained before the detected first path for partial CIR captures"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/app.c
    Console/cir_protocol.c
    Console/console_protocol.c
    Console/console_service.c
    ${HOPWINS_ROLE_SOURCE}
    UWB/uwb_profile.c
    UWB/uwb_service.c
)

if(HOPWINS_BOARD_EMBED_FPGA_IMAGE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        FPGA/fpga_image.c
        FPGA/fpga_service.c
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
    Console
    FPGA
    Roles
    UWB
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC=$<BOOL:${HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC}>
    HOPWINS_UWB_CIR_SAMPLE_COUNT=${HOPWINS_UWB_CIR_SAMPLE_COUNT}
    HOPWINS_UWB_CIR_PRE_FIRST_PATH_SAMPLES=${HOPWINS_UWB_CIR_PRE_FIRST_PATH_SAMPLES}
)
