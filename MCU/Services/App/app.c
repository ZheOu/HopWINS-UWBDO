/**
  ******************************************************************************
  * @file           : app.c
  * @brief          : Top-level application service
  ******************************************************************************
  */

#include "app.h"

#include "board.h"
#include "console_service.h"
#include "workflow_dispatcher.h"

bool app_init(const app_config_t *config)
{
  bool initialized;

  console_service_init();
  if (config == NULL) {
    console_service_write("APP ERROR: missing configuration\r\n");
    console_service_finish_boot();
    return false;
  }

  console_service_report_firmware_profile(
      board_get_description(),
      workflow_dispatcher_name(config->workflow),
      HOPWINS_BUILD_TYPE_NAME);
  initialized = workflow_dispatcher_init(config);
  if (!initialized) {
    console_service_write("APP ERROR: workflow is incompatible with PCB\r\n");
  } else {
    console_service_write("BOOT: APPLICATION READY\r\n");
  }
  console_service_finish_boot();
  return initialized;
}

void app_process(void)
{
  workflow_dispatcher_process();
  console_service_process();
}
