/**
  ******************************************************************************
  * @file           : app.c
  * @brief          : Top-level application service
  ******************************************************************************
  */

#include "app.h"

#include "board.h"
#include "console_service.h"
#include "role_service.h"

void app_init(void)
{
  console_service_init();
  console_service_report_firmware_profile(
      board_get_capabilities(),
      role_service_name(),
      HOPWINS_BUILD_TYPE_NAME);
  role_service_init();
}

void app_process(void)
{
  role_service_process();
  console_service_process();
}
