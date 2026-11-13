#include "Controllers/UIController/UIConsoleCommands.h"
#include "Controllers/UIController/BootAnimation.h"
#include "Controllers/UIController/IUIController.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "esp_log.h"
#include <stdio.h>
#include "Core/Localization/Localization.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "UIConsoleCmd";

static bool UI_CommandArguments(int argc, char **argv, int minimum, int maximum) {
  if (argc < minimum || argc > maximum || !argv) return false;
  for (int i = 0; i < argc; ++i) if (!argv[i] || !*argv[i]) return false;
  return true;
}
static bool UI_CommandFloat(const char *text, float *out) {
  if (!text || !*text || !out) return false;
  errno = 0; char *end = NULL; float value = strtof(text, &end);
  if (errno == ERANGE || end == text || *end || !isfinite(value)) return false;
  *out = value; return true;
}
static bool UI_CommandBoolean(const char *text, bool *out) {
  if (!text || !out) return false;
  if (!strcasecmp(text, "ON") || !strcmp(text, "1")) { *out = true; return true; }
  if (!strcasecmp(text, "OFF") || !strcmp(text, "0")) { *out = false; return true; }
  return false;
}

/**
 * @brief Handles 'UI STATUS' command.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, non-zero on failure.
 */
static int do_ui_status(int argc, char **argv) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  (void)argc; (void)argv;
  IUIController *ui = UIController_GetModule();
  if (!ui) {
    ESP_LOGE(TAG, "UIController service not found");
    LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_SERVICE_NOT_FOUND, "UIController: service not found\n");
    return 1;
  }

  if (!ui->IsScreenOn || !ui->base.GetState) return 1;
  bool isScreenOn = ui->IsScreenOn(ui);
  ESP_LOGI(TAG, "Inspecting UI status: screen is %s", isScreenOn ? "ACTIVE" : "SUSPENDED");

  if (argc == 2 && argv && argv[1] && !strcmp(argv[1], "--machine")) {
    /* This record reflects owner readbacks, not a requested screen state. */
    printf("RACOOL_CLI v=1 type=ui screen=%s service=%d boot=%s\n",
           isScreenOn ? "active" : "suspended", (int)ui->base.GetState((IService *)ui),
           BootAnimation_IsRunning() ? "running" : "idle");
    return 0;
  }

  LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_STATUS, "=== UIController Status ===\n");
  LOC_PRINTF(catalog, LOC_CLI_UI_SCREEN_STATE, "Screen State:  %s\n", isScreenOn ? Localization_Text(catalog, LOC_CLI_UI_VALUE_ACTIVE) : Localization_Text(catalog, LOC_CLI_UI_VALUE_SUSPENDED));
  LOC_PRINTF(catalog, LOC_CLI_UI_SERVICE_STATE, "Service State: %d\n", (int)ui->base.GetState((IService *)ui));
  LOC_PRINTF(catalog, LOC_CLI_UI_BOOT_ANIMATION, "Boot Animation: %s\n", BootAnimation_IsRunning() ? Localization_Text(catalog, LOC_CLI_UI_VALUE_RUNNING) : Localization_Text(catalog, LOC_CLI_UI_VALUE_IDLE));
  return 0;
}

/**
 * @brief Handles 'UI WAKE' command.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, non-zero on failure.
 */
static int do_ui_wake(int argc, char **argv) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  (void)argc; (void)argv;
  IUIController *ui = UIController_GetModule();
  if (!ui) {
    ESP_LOGE(TAG, "UIController service not found");
    LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_SERVICE_NOT_FOUND, "UIController: service not found\n");
    return 1;
  }

  if (!ui->PostEvent) return 1;
  UIEvent event = { .type = UI_EVENT_SCREEN_WAKEUP };
  int32_t res = ui->PostEvent(ui, &event);
  if (res == 0) {
    ESP_LOGI(TAG, "Screen wakeup event posted successfully");
    LOC_PRINTF(catalog, LOC_CLI_UI_POSTED_SCREEN_WAKEUP_EVENT_SUCCESSFULLY, "Posted screen wakeup event successfully\n");
    return 0;
  } else {
    ESP_LOGE(TAG, "Failed to post screen wakeup event (Error: %ld)", (long)res);
    LOC_PRINTF(catalog, LOC_CLI_UI_FAILED_TO_POST_WAKEUP_EVENT_ERROR, "Failed to post wakeup event (Error: %ld)\n", (long)res);
    return 1;
  }
}

/**
 * @brief Handles 'UI EVENT <type> [value]' command.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, non-zero on failure.
 */
static int do_ui_event(int argc, char **argv) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  if (!UI_CommandArguments(argc, argv, 2, 5)) {
    LOC_PRINTF(catalog, LOC_CLI_UI_USAGE_UI_EVENT_TEMP_ENCODER_CLICK_RELAY_ZIGBEE_WAKE_VALUE, "Usage: UI EVENT <TEMP|ENCODER|CLICK|RELAY|ZIGBEE|WAKE> [value]\n");
    return 1;
  }

  IUIController *ui = UIController_GetModule();
  if (!ui) {
    ESP_LOGE(TAG, "UIController service not found");
    LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_SERVICE_NOT_FOUND, "UIController: service not found\n");
    return 1;
  }

  if (!ui->PostEvent) return 1;
  UIEvent event;
  memset(&event, 0, sizeof(event));

  if (strcasecmp(argv[1], "TEMP") == 0) {
    event.type = UI_EVENT_TEMP_UPDATE;
    event.data.temp.objectTemp = 25.0f;
    event.data.temp.ambientTemp = 22.0f;
    event.data.temp.cpuTemp = 35.0f;
    if ((argc >= 3 && !UI_CommandFloat(argv[2], &event.data.temp.objectTemp)) ||
        (argc >= 4 && !UI_CommandFloat(argv[3], &event.data.temp.ambientTemp)) ||
        (argc >= 5 && !UI_CommandFloat(argv[4], &event.data.temp.cpuTemp))) return 1;
    ESP_LOGI(TAG, "Mocking TEMP event: Radiator=%.1f, Ambient=%.1f, CPU=%.1f",
             event.data.temp.objectTemp, event.data.temp.ambientTemp, event.data.temp.cpuTemp);
    LOC_PRINTF(catalog, LOC_CLI_UI_POSTING_TEMP_EVENT_RADIATOR, "Posting TEMP event: Radiator=%.1f, Ambient=%.1f, CPU=%.1f\n",
           event.data.temp.objectTemp, event.data.temp.ambientTemp, event.data.temp.cpuTemp);
  } else if (strcasecmp(argv[1], "ENCODER") == 0) {
    event.type = UI_EVENT_ENCODER_MOVE;
    if (argc > 3) return 1;
    event.data.encoder.difference = 1;
    if (argc == 3) {
      errno = 0; char *end = NULL; long value = strtol(argv[2], &end, 10);
      if (errno == ERANGE || end == argv[2] || *end || value < INT8_MIN || value > INT8_MAX) return 1;
      event.data.encoder.difference = (int32_t)value;
    }
    ESP_LOGI(TAG, "Mocking ENCODER event: Difference=%ld", (long)event.data.encoder.difference);
    LOC_PRINTF(catalog, LOC_CLI_UI_POSTING_ENCODER_EVENT_DIFFERENCE, "Posting ENCODER event: Difference=%ld\n", (long)event.data.encoder.difference);
  } else if (strcasecmp(argv[1], "CLICK") == 0) {
    if (argc != 2) return 1;
    event.type = UI_EVENT_ENCODER_CLICK;
    ESP_LOGI(TAG, "Mocking CLICK event");
    LOC_PRINTF(catalog, LOC_CLI_UI_POSTING_CLICK_EVENT, "Posting CLICK event\n");
  } else if (strcasecmp(argv[1], "RELAY") == 0) {
    event.type = UI_EVENT_RELAY_STATE;
    if (argc > 3) return 1;
    event.data.relay.isOpen = true;
    if (argc == 3 && !UI_CommandBoolean(argv[2], &event.data.relay.isOpen)) return 1;
    ESP_LOGI(TAG, "Mocking RELAY event: IsOpen=%s", event.data.relay.isOpen ? "true" : "false");
    LOC_PRINTF(catalog, LOC_CLI_UI_POSTING_RELAY_EVENT_ISOPEN, "Posting RELAY event: IsOpen=%s\n", event.data.relay.isOpen ? Localization_Text(catalog, LOC_CLI_UI_VALUE_TRUE) : Localization_Text(catalog, LOC_CLI_UI_VALUE_FALSE));
  } else if (strcasecmp(argv[1], "ZIGBEE") == 0) {
    event.type = UI_EVENT_ZIGBEE_STATE;
    if (argc > 3) return 1;
    event.data.zigbee.isConnected = true;
    if (argc == 3 && !UI_CommandBoolean(argv[2], &event.data.zigbee.isConnected)) return 1;
    ESP_LOGI(TAG, "Mocking ZIGBEE event: IsConnected=%s", event.data.zigbee.isConnected ? "true" : "false");
    LOC_PRINTF(catalog, LOC_CLI_UI_POSTING_ZIGBEE_EVENT_ISCONNECTED, "Posting ZIGBEE event: IsConnected=%s\n", event.data.zigbee.isConnected ? Localization_Text(catalog, LOC_CLI_UI_VALUE_TRUE) : Localization_Text(catalog, LOC_CLI_UI_VALUE_FALSE));
  } else if (strcasecmp(argv[1], "WAKE") == 0) {
    if (argc != 2) return 1;
    event.type = UI_EVENT_SCREEN_WAKEUP;
    ESP_LOGI(TAG, "Mocking WAKE event");
    LOC_PRINTF(catalog, LOC_CLI_UI_POSTING_WAKE_EVENT, "Posting WAKE event\n");
  } else {
    ESP_LOGW(TAG, "Unknown event type mock requested: %s", argv[1]);
    LOC_PRINTF(catalog, LOC_CLI_UI_UNKNOWN_EVENT_TYPE, "Unknown event type: %s\n", argv[1]);
    return 1;
  }

  int32_t res = ui->PostEvent(ui, &event);
  if (res == 0) {
    ESP_LOGI(TAG, "Event posted successfully");
    LOC_PRINTF(catalog, LOC_CLI_UI_EVENT_POSTED_SUCCESSFULLY, "Event posted successfully\n");
    return 0;
  } else {
    ESP_LOGE(TAG, "Failed to post event (Error: %ld)", (long)res);
    LOC_PRINTF(catalog, LOC_CLI_UI_FAILED_TO_POST_EVENT_ERROR, "Failed to post event (Error: %ld)\n", (long)res);
    return 1;
  }
}

/**
 * @brief Handles 'UI CONFIG' command to print current UI configurations.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, non-zero on failure.
 */
static int do_ui_config(int argc, char **argv) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  (void)argc; (void)argv;
  IUIController *ui = UIController_GetModule();
  if (!ui) {
    ESP_LOGE(TAG, "UIController service not found");
    LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_SERVICE_NOT_FOUND, "UIController: service not found\n");
    return 1;
  }

  ESP_LOGI(TAG, "Printing UI Controller configurations");
  IConfigurationManager *configMgr = ConfigurationManager_GetInstance();
  SystemSettings snapshot;
  if (!configMgr || !configMgr->GetSnapshot || configMgr->GetSnapshot(configMgr, &snapshot) != ESP_OK) {
    LOC_PRINTF(catalog, LOC_CLI_UI_UI_CONFIGURATION_SNAPSHOT_UNAVAILABLE, "UI configuration snapshot unavailable\n");
    return 1;
  }
  const SystemSettings *settings = &snapshot;
  uint32_t timeout = settings->system.oled_timeout_sec >= OLED_TIMEOUT_MIN_SEC
      ? settings->system.oled_timeout_sec : uiControllerInstance.config.screenTimeoutSec;
  if (timeout < OLED_TIMEOUT_MIN_SEC) timeout = OLED_TIMEOUT_MIN_SEC;
  if (argc==2 && argv && argv[1] && !strcmp(argv[1],"--machine")) {
    printf("RACOOL_CLI v=1 type=ui_config screen_timeout_s=%lu display_mode=%u\n",
        (unsigned long)timeout,(unsigned)settings->system.display_mode);
    return 0;
  }
  LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_CONFIGURATION, "=== UIController Configuration ===\n");
  LOC_PRINTF(catalog, LOC_CLI_UI_REFRESH_INTERVAL, "Refresh Interval:    %ld ms\n", (long)uiControllerInstance.config.refreshIntervalMs);
  LOC_PRINTF(catalog, LOC_CLI_UI_SCREEN_TIMEOUT, "Screen Timeout:      %lu sec\n", (unsigned long)timeout);
  LOC_PRINTF(catalog, LOC_CLI_UI_DISPLAY_MODE, "Display Mode:        %u (1=auto, 2=always on)\n", settings->system.display_mode);
  LOC_PRINTF(catalog, LOC_CLI_UI_TEMP_HYSTERESIS, "Temp Hysteresis:     %.2f °C\n", uiControllerInstance.config.tempRefreshHysteresis);
  if (settings) {
    LOC_PRINTF(catalog, LOC_CLI_UI_COLD_TEMP_RANGE, "Cold Temp range:    %.1f-%.1f °C\n", settings->hardware.ext_led_temp_cold_min, settings->hardware.ext_led_temp_cold_max);
    LOC_PRINTF(catalog, LOC_CLI_UI_WARM_TEMP_RANGE, "Warm Temp range:    %.1f-%.1f °C\n", settings->hardware.ext_led_temp_warm_min, settings->hardware.ext_led_temp_warm_max);
    LOC_PRINTF(catalog, LOC_CLI_UI_HOT_TEMP_RANGE, "Hot Temp range:     %.1f-%.1f °C\n", settings->hardware.ext_led_temp_hot_min, settings->hardware.ext_led_temp_hot_max);
    LOC_PRINTF(catalog, LOC_CLI_UI_DANGER_THRESHOLD, "Danger threshold:   %.1f °C\n", settings->hardware.ext_led_temp_danger);
    LOC_PRINTF(catalog, LOC_CLI_UI_FREEZE_THRESHOLD, "Freeze threshold:   %.1f °C\n", settings->hardware.ext_led_temp_freeze);
  } else {
    LOC_PRINTF(catalog, LOC_CLI_UI_THERMAL_CONFIGURATION_UNAVAILABLE, "Thermal configuration: unavailable\n");
  }
  LOC_PRINTF(catalog, LOC_CLI_UI_EVENT_QUEUE_SIZE, "Event Queue Size:    %ld\n", (long)uiControllerInstance.config.eventQueueSize);
  LOC_PRINTF(catalog, LOC_CLI_UI_MUTEX_TIMEOUT, "Mutex Timeout:       %ld ms\n", (long)uiControllerInstance.config.mutexTimeoutMs);
  return 0;
}

/**
 * @brief Handles 'UI TEST' command.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, non-zero on failure.
 */
static int do_ui_test(int argc, char **argv) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  (void)argc; (void)argv;
  IUIController *ui = UIController_GetModule();
  if (!ui) {
    ESP_LOGE(TAG, "UIController service not found");
    LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_SERVICE_NOT_FOUND, "UIController: service not found\n");
    return 1;
  }

  ESP_LOGI(TAG, "Running UIController self-test...");
  LOC_PRINTF(catalog, LOC_CLI_UI_RUNNING_UICONTROLLER_SELF_TEST, "Running UIController self-test...\n");
  if (!ui->base.CheckHealth) return 1;
  int32_t res = ui->base.CheckHealth((IService *)ui);
  if (res == 0) {
    ESP_LOGI(TAG, "UIController self-test PASSED");
    LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_SELF_TEST_PASSED, "UIController self-test PASSED\n");
    return 0;
  } else {
    ESP_LOGE(TAG, "UIController self-test FAILED (Error: %ld)", (long)res);
    LOC_PRINTF(catalog, LOC_CLI_UI_UICONTROLLER_SELF_TEST_FAILED_ERROR, "UIController self-test FAILED (Error: %ld)\n", (long)res);
    return 1;
  }
}

/**
 * @brief Top-level UI command dispatcher.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, non-zero on failure.
 */
static int dispatch_ui(int argc, char **argv) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  if (!UI_CommandArguments(argc, argv, 1, 6)) return 1;
  if (argc > 2 && strcasecmp(argv[1], "EVENT") != 0 &&
      !(argc == 3 && (!strcasecmp(argv[1],"STATUS") || !strcasecmp(argv[1],"CONFIG")) && !strcmp(argv[2], "--machine"))) return 1;
  if (argc < 2) {
    LOC_PRINTF(catalog, LOC_CLI_UI_USAGE_UI_SUBCOMMAND, "Usage: UI <subcommand>\n");
    LOC_PRINTF(catalog, LOC_CLI_UI_SUB_COMMANDS, "Sub-commands:\n");
    LOC_PRINTF(catalog, LOC_CLI_UI_STATUS_DISPLAY_SCREEN_AND_SERVICE_STATUS, "  STATUS              — Display screen and service status\n");
    LOC_PRINTF(catalog, LOC_CLI_UI_WAKE_EXPLICITLY_WAKE_UP_SCREEN, "  WAKE                — Explicitly wake up screen\n");
    LOC_PRINTF(catalog, LOC_CLI_UI_EVENT_TYPE_VAL_POST_A_MOCK_EVENT_TO_THE_UI_CONTROLLER, "  EVENT <type> [val]  — Post a mock event to the UI controller\n");
    LOC_PRINTF(catalog, LOC_CLI_UI_CONFIG_DISPLAY_CURRENT_CONFIGURATION_THRESHOLDS, "  CONFIG              — Display current configuration thresholds\n");
    LOC_PRINTF(catalog, LOC_CLI_UI_TEST_RUN_UICONTROLLER_HEALTH_CHECK_SELF_TEST, "  TEST                — Run UIController health check self-test\n");
    return 0;
  }

  if (strcasecmp(argv[1], "STATUS") == 0) return do_ui_status(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "WAKE")   == 0) return do_ui_wake(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "EVENT")  == 0) return do_ui_event(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "CONFIG") == 0) return do_ui_config(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "TEST")   == 0) return do_ui_test(argc - 1, &argv[1]);

  ESP_LOGW(TAG, "Unknown sub-command received: %s", argv[1]);
  LOC_PRINTF(catalog, LOC_CLI_UI_UNKNOWN_SUB_COMMAND, "Unknown sub-command: %s\n", argv[1]);
  LOC_PRINTF(catalog, LOC_CLI_UI_RUN_UI_WITHOUT_ARGUMENTS_FOR_USAGE, "Run 'UI' without arguments for usage.\n");
  return 1;
}

static int cmd_ui(int argc,char **argv) {
  if (!UI_CommandArguments(argc,argv,1,7)) return 1;
  bool machine=argc>=3 && !strcmp(argv[argc-1],"--machine");
  if (!machine || !strcasecmp(argv[1],"STATUS") || !strcasecmp(argv[1],"CONFIG"))
    return dispatch_ui(argc,argv);
  const char *commands[]={"WAKE","EVENT","TEST"};
  for (unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i) {
    if (strcasecmp(argv[1],commands[i])) continue;
    int result=dispatch_ui(argc-1,argv);
    printf("RACOOL_CLI v=1 type=ui_command command=%s result=%s error=%d\n",
        commands[i],result==0 ? (i<2 ? "queued" : "passed") : "rejected",result);
    return result;
  }
  return 1;
}

esp_err_t UIConsoleCommands_Register(IDebugConsole *console) {
  if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_ARG;

  static const esp_console_cmd_t ui_cmds[] = {
      {
          .command = "UI",
          .help    = "UIController inspection and control (STATUS/WAKE/EVENT/CONFIG/TEST)",
          .func    = &cmd_ui,
          .hint    = "<subcommand>",
      },
      {
          .command = "UC",
          .help    = "UIController commands (short alias for UI)",
          .func    = &cmd_ui,
          .hint    = "<subcommand>",
      },
  };

  for (int i = 0; i < (int)(sizeof(ui_cmds) / sizeof(ui_cmds[0])); i++) {
    esp_err_t result = console->RegisterCommandWithCategory(console, &ui_cmds[i], CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
  }
  return ESP_OK;
}
