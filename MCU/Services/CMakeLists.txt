option(
    HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
    "Run the temporary DW3000 reference-clock diagnostic during startup"
    OFF
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/app.c
    Console/console_protocol.c
    Console/console_service.c
    FPGA/fpga_image.c
    FPGA/fpga_service.c
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
)
