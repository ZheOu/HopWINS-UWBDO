target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Composition root
    App/app.c

    # Replaceable policies
    Algorithms/ClockDiscipline/do_loop_strategy.c
    Algorithms/Timestamp/uwb_timestamp_dw.c
    Algorithms/Timestamp/uwb_timestamp_estimator.c

    # Reusable physical-device services
    Devices/CLK/clock_service.c
    Devices/FPGA/fpga_image.c
    Devices/FPGA/fpga_service.c
    Devices/UWB/uwb_profile.c
    Devices/UWB/uwb_service.c

    # Host communication and protocol encoders
    Communication/Serial/capture_output.c
    Communication/Serial/hcir_protocol.c
    Communication/Serial/console_protocol.c
    Communication/Serial/console_service.c

    # Explicit hardware diagnostics
    Diagnostics/UWB/uwb_sts_diagnostic_service.c

    # Runtime workflows
    Workflows/DO/do_clock_tracking_service.c
    Workflows/do_follower_service.c
    Workflows/do_leader_service.c
    Workflows/workflow_dispatcher.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
    Algorithms/ClockDiscipline
    Algorithms/Timestamp
    Devices/CLK
    Devices/FPGA
    Devices/UWB
    Communication/Serial
    Diagnostics/UWB
    Workflows
    Workflows/DO
)
