target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/app.c
    CLK/clock_service.c
    Console/cir_protocol.c
    Console/console_protocol.c
    Console/console_service.c
    FPGA/fpga_image.c
    FPGA/fpga_service.c
    Roles/do_follower_service.c
    Roles/do_leader_service.c
    Roles/role_service.c
    UWB/uwb_profile.c
    UWB/uwb_service.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
    CLK
    Console
    FPGA
    Roles
    UWB
)
