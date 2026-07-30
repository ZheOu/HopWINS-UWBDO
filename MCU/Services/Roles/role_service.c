/**
  ******************************************************************************
  * @file           : role_service.c
  * @brief          : Runtime application-workflow dispatcher
  ******************************************************************************
  */

#include "role_service.h"

#include "board.h"
#include "do_follower_service.h"
#include "do_leader_service.h"

static app_workflow_t s_workflow;
static bool s_initialized;

const char *role_service_name(app_workflow_t workflow)
{
  switch (workflow) {
    case APP_WORKFLOW_DO_LEADER:
      return "DO-Leader";
    case APP_WORKFLOW_DO_FOLLOWER:
      return "DO-Follower";
    default:
      return "Invalid";
  }
}

bool role_service_init(const app_config_t *config)
{
  const board_capabilities_t *board = board_get_capabilities();

  s_initialized = false;
  if ((config == NULL) || (board == NULL)) {
    return false;
  }

  s_workflow = config->workflow;
  switch (s_workflow) {
    case APP_WORKFLOW_DO_LEADER:
      do_leader_service_init(config);
      break;

    case APP_WORKFLOW_DO_FOLLOWER:
      if (!board->has_fpga ||
          !board->has_clock_control ||
          !board->has_external_clock_counter ||
          (board->installed_xo == BOARD_CLOCK_XO_NONE)) {
        return false;
      }
      do_follower_service_init(config);
      break;

    default:
      return false;
  }

  s_initialized = true;
  return true;
}

void role_service_process(void)
{
  if (!s_initialized) {
    return;
  }

  switch (s_workflow) {
    case APP_WORKFLOW_DO_LEADER:
      do_leader_service_process();
      break;
    case APP_WORKFLOW_DO_FOLLOWER:
      do_follower_service_process();
      break;
    default:
      s_initialized = false;
      break;
  }
}
