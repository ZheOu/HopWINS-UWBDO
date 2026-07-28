option(
    HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
    "Run the temporary DW3000 reference-clock diagnostic during startup"
    OFF
)

option(
    HOPWINS_ENABLE_PERIODIC_UWB_TX
    "Configure the DW3000 and continuously transmit the default UWB test frame"
    ON
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/app.c
    Console/console_protocol.c
    Console/console_service.c
    FPGA/fpga_image.c
    FPGA/fpga_service.c
    UWB/uwb_profile.c
    UWB/uwb_service.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
    Console
    FPGA
    UWB
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC=$<BOOL:${HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC}>
    HOPWINS_ENABLE_PERIODIC_UWB_TX=$<BOOL:${HOPWINS_ENABLE_PERIODIC_UWB_TX}>
)
