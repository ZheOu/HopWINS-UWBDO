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

bool app_init(const app_config_t *config)
{
  bool initialized;

  console_service_init();
  if (config == NULL) {
    console_service_write("APP ERROR: missing configuration\r\n");
    return false;
  }

  console_service_report_firmware_profile(
      board_get_capabilities(),
      role_service_name(config->workflow),
      HOPWINS_BUILD_TYPE_NAME);
  initialized = role_service_init(config);
  if (!initialized) {
    console_service_write("APP ERROR: workflow is incompatible with PCB\r\n");
  }
  return initialized;
}

void app_process(void)
{
  role_service_process();
  console_service_process();
}
