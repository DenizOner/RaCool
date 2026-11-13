#include "Controllers/ClimateController/FanCommands.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
/**
 * @file RelayConsoleCommands.c
 * @brief Debug Console commands implementation for Relay Hardware Module.
 */

#include "Hardware/Relay/RelayInternal.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include <stdio.h>
#include "Core/Localization/Localization.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>

static bool Relay_ParseDuration(const char *text, uint32_t maximum,
                                uint32_t *value) {
  if (!text || !*text || !value) return false;
  for (const char *p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
  }
  errno = 0;
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);
  if (errno == ERANGE || !end || *end || parsed > UINT32_MAX ||
      parsed > maximum) return false;
  *value = (uint32_t)parsed;
  return true;
}

static int cmd_relay_info(int argc, char **argv) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  if (argc<1 || argc>3 || !argv) return 1;
  for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return 1;
  bool machine=argc>=2 && !strcmp(argv[argc-1],"--machine");
  int tokens=argc-(machine ? 1 : 0);
  if (tokens!=1 && !(tokens==2 && !strcasecmp(argv[1],"INFO"))) return 1;
  if (Relay_Lock() != ESP_OK) return 1;
  gpio_num_t pin = relayService.relay_pin;
  RelayState state = relayService.current_status;
  RelayMode mode = relayService.current_mode;
  xSemaphoreGive(relayService.mutex);
  if (state<RELAY_STATE_CLOSED || state>RELAY_STATE_PULSING ||
      mode<RELAY_MODE_MANUAL || mode>RELAY_MODE_AUTOMATIC) return 1;
  RelayStats stats;
  if (Relay_GetStats(Relay_GetModule(), &stats) != ESP_OK) return 1;
  if (machine) {
    printf("RACOOL_CLI v=1 type=relay_status state=%d mode=%d pin=%d level=%d\n",
      (int)state,(int)mode,(int)pin,pin==GPIO_NUM_NC ? -1 : gpio_get_level(pin));
    return 0;
  }
  LOC_PRINTF(catalog,LOC_CLI_RELAY_TITLE, "\n--- Relay Hardware Status ---\n");
  LOC_PRINTF(catalog,LOC_CLI_RELAY_GPIO, "GPIO Pin: %d\n", pin);
  const char *st_str = Localization_Text(catalog,LOC_CLI_RELAY_VALUE_CLOSED);
  if (state == RELAY_STATE_OPEN) st_str = Localization_Text(catalog,LOC_CLI_RELAY_VALUE_OPEN);
  else if (state == RELAY_STATE_PULSING) st_str = Localization_Text(catalog,LOC_CLI_RELAY_VALUE_PULSING);
  LOC_PRINTF(catalog,LOC_CLI_RELAY_STATE, "State: %s\n", st_str);
  LOC_PRINTF(catalog,LOC_CLI_RELAY_MODE, "Mode: %s\n", (mode == RELAY_MODE_AUTOMATIC) ? Localization_Text(catalog,LOC_CLI_RELAY_VALUE_AUTOMATIC) : Localization_Text(catalog,LOC_CLI_RELAY_VALUE_MANUAL));
  if (pin != GPIO_NUM_NC) {
    LOC_PRINTF(catalog,LOC_CLI_RELAY_GPIO_LEVEL, "Actual GPIO Level: %d\n", gpio_get_level(pin));
  }

  LOC_PRINTF(catalog,LOC_CLI_RELAY_SWITCHES, "Total Switches: %lu\n", (unsigned long)stats.total_switches);
  LOC_PRINTF(catalog,LOC_CLI_RELAY_ACTIVE_TIME, "Total Active Time: %llu ms\n", (unsigned long long)stats.total_active_time_ms);
  LOC_PRINTF(catalog,LOC_CLI_RELAY_SAFETY, "Safety Triggers: %lu\n", (unsigned long)stats.safety_triggers_count);
  LOC_PRINTF(catalog,LOC_CLI_RELAY_REMAINING, "Anti-Short Cycle Delay Remaining: %lu ms\n", (unsigned long)Relay_GetAntiShortCycleRemainingMs(Relay_GetModule()));
  LOC_PRINTF(catalog,LOC_CLI_RELAY_SEPARATOR, "-----------------------------\n");
  return 0;
}

static int cmd_relay_ctrl(int argc, char **argv) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  if (argc<1 || argc>3 || !argv) return 1;
  for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return 1;
  if (argc==3 && !strcasecmp(argv[1],"INFO") && !strcmp(argv[2],"--machine"))
    return cmd_relay_info(argc,argv);
  if (argc < 2) {
    LOC_PRINTF(catalog,LOC_CLI_RELAY_USAGE, "Usage: RELAY <ON|OFF|TOGGLE|PULSE [ms]|AUTO_OFF [ms]|MODE <MANUAL|AUTO>|RESET_STATS|TEST>\n");
    return 1;
  }
  IRelay *relay = Relay_GetModule();
  if (argc == 2 && strcasecmp(argv[1], "INFO") == 0) return cmd_relay_info(argc, argv);
  esp_err_t result = ESP_ERR_INVALID_ARG;
  bool admission = false;
  IClimateController *controller=(IClimateController *)ServiceLocator_GetService(SVC_NAME_CLIMATE_CONTROLLER);
  IConfigurationManager *owner=ConfigurationManager_GetInstance();
  bool timed = strcasecmp(argv[1], "PULSE") == 0 ||
               strcasecmp(argv[1], "AUTO_OFF") == 0;
  bool mode = strcasecmp(argv[1], "MODE") == 0;
  if ((timed && argc > 3) || (mode && argc != 3) ||
      (!timed && !mode && argc != 2)) return 1;
  if (strcasecmp(argv[1], "ON") == 0) {
    result = FanCommands_Dispatch(owner,(FanCommand){FAN_COMMAND_MODE,CLIMATE_FAN_MANUAL_ON,0},false);
    admission=true;
  } else if (strcasecmp(argv[1], "OFF") == 0) {
    result = FanCommands_Dispatch(owner,(FanCommand){FAN_COMMAND_MODE,CLIMATE_FAN_MANUAL_OFF,0},false);
    admission=true;
  } else if (strcasecmp(argv[1], "TOGGLE") == 0) {
    bool current;
    if (!relay->ReadState || relay->ReadState(relay,&current)!=ESP_OK) return 1;
    result=FanCommands_Dispatch(owner,(FanCommand){FAN_COMMAND_MODE,current ? CLIMATE_FAN_MANUAL_OFF : CLIMATE_FAN_MANUAL_ON,0},false);
    admission=true;
  } else if (strcasecmp(argv[1], "PULSE") == 0) {
    uint32_t ms = RELAY_DEFAULT_PULSE_DURATION_MS;
    if (argc == 3 && !Relay_ParseDuration(argv[2], RELAY_MAX_PULSE_DURATION_MS, &ms)) return 1;
    result=controller && controller->RequestFanAction ? controller->RequestFanAction(controller,FAN_ACTION_PULSE,ms) : ESP_ERR_INVALID_STATE;
    admission=true;
  } else if (strcasecmp(argv[1], "AUTO_OFF") == 0) {
    uint32_t ms = RELAY_DEFAULT_AUTO_OFF_TIMEOUT_MS;
    if (argc == 3 && !Relay_ParseDuration(argv[2], UINT32_MAX, &ms)) return 1;
    result=controller && controller->RequestFanAction ? controller->RequestFanAction(controller,FAN_ACTION_AUTO_OFF,ms) : ESP_ERR_INVALID_STATE;
    admission=true;
  } else if (strcasecmp(argv[1], "MODE") == 0 && argc >= 3) {
    if (strcasecmp(argv[2], "AUTO") == 0 || strcasecmp(argv[2], "AUTOMATIC") == 0) {
      result=controller && controller->RequestFanAction ? controller->RequestFanAction(controller,FAN_ACTION_RELAY_MODE,RELAY_MODE_AUTOMATIC) : ESP_ERR_INVALID_STATE;
      admission=true;
    } else if (strcasecmp(argv[2], "MANUAL") == 0) {
      result=controller && controller->RequestFanAction ? controller->RequestFanAction(controller,FAN_ACTION_RELAY_MODE,RELAY_MODE_MANUAL) : ESP_ERR_INVALID_STATE;
      admission=true;
    } else return 1;
  } else if (strcasecmp(argv[1], "RESET_STATS") == 0) {
    result=controller && controller->RequestFanAction ? controller->RequestFanAction(controller,FAN_ACTION_RESET_STATS,0) : ESP_ERR_INVALID_STATE;
    admission=true;
  } else if (strcasecmp(argv[1], "TEST") == 0) {
    result=controller && controller->RequestFanAction ? controller->RequestFanAction(controller,FAN_ACTION_SELF_TEST,0) : ESP_ERR_INVALID_STATE;
    admission=true;
  } else {
    LOC_PRINTF(catalog,LOC_CLI_RELAY_UNKNOWN, "Unknown command or missing arguments. Usage: RELAY <ON|OFF|TOGGLE|PULSE|AUTO_OFF|MODE|RESET_STATS|TEST>\n");
    return 1;
  }
  if (result != ESP_OK) {
    LOC_PRINTF(catalog,LOC_CLI_RELAY_FAILED, "Relay operation failed: 0x%x\n", (unsigned)result);
    return 1;
  }
  LOC_PRINTF(catalog,LOC_CLI_RELAY_RESULT, "%s\n",admission ? Localization_Text(catalog,LOC_CLI_RELAY_VALUE_ADMITTED) : Localization_Text(catalog,LOC_CLI_RELAY_VALUE_APPLIED));
  return 0;
}

esp_err_t Relay_RegisterCommands(void) {
  static bool info_registered = false, control_registered = false;
  if (info_registered && control_registered) return ESP_OK;

  IDebugConsole *console = DebugConsole_GetModule();
  if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_NOT_FOUND;

  static const esp_console_cmd_t cmd_info = {
    .command = "relay_info",
    .help = "Show relay status and configuration",
    .hint = NULL,
    .func = &cmd_relay_info,
    .argtable = NULL
  };

  static const esp_console_cmd_t cmd_ctrl = {
    .command = "RELAY",
    .help = "Control relay: RELAY <ON|OFF|TOGGLE|PULSE [ms]|AUTO_OFF [ms]|MODE|RESET_STATS|TEST>",
    .hint = NULL,
    .func = &cmd_relay_ctrl,
    .argtable = NULL
  };

  esp_err_t result;
  if (!info_registered) {
    result = console->RegisterCommandWithCategory(console, &cmd_info, CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
    info_registered = true;
  }
  if (!control_registered) {
    result = console->RegisterCommandWithCategory(console, &cmd_ctrl, CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
    control_registered = true;
  }
  return ESP_OK;
}
