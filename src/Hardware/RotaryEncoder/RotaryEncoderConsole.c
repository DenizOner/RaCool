/**
 * @file RotaryEncoderConsole.c
 * @brief Debug console commands for the RotaryEncoder module.
 *
 * Provides ENC (info), ENCRESET, ENCCONFIG, ENCSIM, ENCACCEL,
 * ENCDEBOUNCE, ENCDIAG, and ENCTEST commands for runtime inspection,
 * configuration, simulation, and self-testing of the encoder subsystem.
 */

#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include <stddef.h>
#include <esp_console.h>
#include <esp_timer.h>
#include <stdio.h>
#include "Core/Localization/Localization.h"
#include <stdlib.h>
#include <string.h>

static bool ParseEncoderUnsigned(const char *text, uint32_t minimum, uint32_t maximum, uint32_t *result) {
    if (!text || !*text || !result) return false;
    uint32_t value = 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10)) return false;
        value = value * 10 + digit;
    }
    if (value < minimum) return false;
    *result = value;
    return true;
}

static bool ParseEncoderBoolean(const char *text, bool *result) {
    if (!text || !result) return false;
    if (!strcasecmp(text, "ON") || !strcasecmp(text, "TRUE") || !strcmp(text, "1")) {
        *result = true;
        return true;
    }
    if (!strcasecmp(text, "OFF") || !strcasecmp(text, "FALSE") || !strcmp(text, "0")) {
        *result = false;
        return true;
    }
    return false;
}

/* ── ENC: Status Overview ────────────────────────────────────────────── */

static int cmd_enc_save(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc != 1 || !argv || !argv[0]) return 1;
    IConfigurationManager *configuration = ConfigurationManager_GetInstance();
    RotaryEncoderRuntimeConfig runtime;
    if (!configuration || !configuration->UpdateFields || !rotaryEncoderService.base.GetRuntimeConfig ||
        rotaryEncoderService.base.GetRuntimeConfig(&rotaryEncoderService.base, &runtime) != ESP_OK) return 1;
    uint8_t acceleration = runtime.accel_enabled ? 1 : 0;
    uint8_t inverted = runtime.invert_direction ? 1 : 0;
    ConfigurationFieldUpdate fields[] = {
        {offsetof(HardwareConfigs, encoder_steps_per_detent), &runtime.steps_per_detent, sizeof(uint8_t)},
        {offsetof(HardwareConfigs, encoder_accel_enabled), &acceleration, sizeof(uint8_t)},
        {offsetof(HardwareConfigs, encoder_accel_threshold_ms), &runtime.accel_threshold_ms, sizeof(uint8_t)},
        {offsetof(HardwareConfigs, encoder_invert_direction), &inverted, sizeof(uint8_t)}
    };
    esp_err_t result = configuration->UpdateFields(configuration, "hardware", fields, 4, true);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ENCODER_DETENT_ACCELERATION_DIRECTION_PERSISTENCE, "Encoder detent/acceleration/direction persistence: %s (0x%x)\n",
           result == ESP_OK ? Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_SAVED) : Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_FAILED), result);
    return result == ESP_OK ? 0 : 1;
}

static int cmd_enc_info(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    bool machine=argc==2 && argv && argv[1] && !strcmp(argv[1],"--machine");
    if ((argc != 1 && !machine) || !argv || !argv[0] || !*argv[0] ||
        !rotaryEncoderService.base.GetStatus || !rotaryEncoderService.base.GetDiagnostics ||
        !rotaryEncoderService.base.GetRuntimeConfig) return 1;

    RotaryEncoderStatus status = rotaryEncoderService.base.GetStatus(&rotaryEncoderService.base);
    RotaryEncoderDiagnostics diagnostics;
    RotaryEncoderRuntimeConfig config;
    memset(&diagnostics, 0, sizeof(diagnostics));
    memset(&config, 0, sizeof(config));

    if (rotaryEncoderService.base.GetDiagnostics(&rotaryEncoderService.base, &diagnostics) != ESP_OK ||
        rotaryEncoderService.base.GetRuntimeConfig(&rotaryEncoderService.base, &config) != ESP_OK) return 1;
    if (machine) {
        printf("RACOOL_CLI v=1 type=encoder_status state=%d steps=%u inverted=%u acceleration=%u threshold_ms=%u rotation_debounce_ms=%u button_debounce_ms=%u position=%ld rotations=%lu clicks=%lu dropped_rotations=%lu dropped_buttons=%lu queue_overflows=%lu\n",
            (int)rotaryEncoderService.state,(unsigned)config.steps_per_detent,config.invert_direction ? 1u : 0u,
            config.accel_enabled ? 1u : 0u,(unsigned)config.accel_threshold_ms,
            (unsigned)config.debounce_rotation_ms,(unsigned)config.debounce_button_ms,
            (long)status.absolute_position,(unsigned long)status.total_rotations,(unsigned long)status.total_clicks,
            (unsigned long)status.dropped_rotation_events,(unsigned long)status.dropped_button_events,(unsigned long)diagnostics.queue_overflows);
        return 0;
    }

    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ROTARY_ENCODER_STATUS, "\n--- Rotary Encoder Status ---\n");
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_STATE, "State: %d\n", rotaryEncoderService.state);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_PINS, "Pins: CLK:%d, DT:%d, SW:%d\n",
           rotaryEncoderService.clk_pin, rotaryEncoderService.dt_pin,
           rotaryEncoderService.sw_pin);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ABSOLUTE_POSITION, "Absolute Position: %ld\n", (long)status.absolute_position);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_TOTAL_ROTATIONS, "Total Rotations: %lu\n", (unsigned long)status.total_rotations);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_TOTAL_CLICKS, "Total Clicks: %lu\n", (unsigned long)status.total_clicks);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_BUTTON_HELD, "Button Held: %s\n", status.button_held ? Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_YES) : Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_NO));
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_DROPPED_ROTATION_EVENTS, "Dropped Rotation Events: %lu\n", (unsigned long)status.dropped_rotation_events);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_DROPPED_BUTTON_EVENTS, "Dropped Button Events: %lu\n", (unsigned long)status.dropped_button_events);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ISR_COUNT, "ISR Count: %lu\n", (unsigned long)diagnostics.isr_count);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ROTATION_EVENTS, "Rotation Events: %lu\n", (unsigned long)diagnostics.rotation_events);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_BUTTON_EVENTS, "Button Events: %lu\n", (unsigned long)diagnostics.button_events);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_QUEUE_OVERFLOWS, "Queue Overflows: %lu\n", (unsigned long)diagnostics.queue_overflows);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_PCNT_OVERFLOWS, "PCNT Overflows: %lu\n", (unsigned long)diagnostics.pcnt_overflow_count);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_STEPS_PER_DETENT, "Steps Per Detent: %u\n", config.steps_per_detent);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_DIRECTION_INVERTED, "Direction Inverted: %s\n", config.invert_direction ? Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_YES) : Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_NO));
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ACCELERATION, "Acceleration: %s threshold=%u max=%u\n",
           config.accel_enabled ? Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_ON) : Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_OFF),
           config.accel_threshold_ms,
           (unsigned)RE_ACCEL_MAX_MULTIPLIER);
    if (status.last_activity_ms > 0) {
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_LAST_ACTIVITY, "Last Activity: %lu ms ago\n",
               (unsigned long)((esp_timer_get_time() / 1000ULL) - status.last_activity_ms));
    } else {
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_LAST_ACTIVITY_DETAILS, "Last Activity: Never\n");
    }
    printf("-----------------------------\n");
    return 0;
}

/* ── ENCRESET: Reset Counters ────────────────────────────────────────── */

static int cmd_enc_reset(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    (void)argv;
    if (argc != 1) {
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_USAGE, "Usage: ENCRESET\n");
        return 1;
    }
    esp_err_t result = rotaryEncoderService.base.Reset(&rotaryEncoderService.base);
    if (result != ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_ENCODER_RESET_FAILED, "Encoder reset failed: 0x%x\n", result);
        return 1;
    }
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ENCODER_COUNTERS_AND_POSITION_RESET_SUCCESSFULLY, "Encoder counters and position reset successfully.\n");
    return 0;
}

/* ── ENCCONFIG: Runtime Configuration ────────────────────────────────── */

static int cmd_enc_config(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc!=3 || !argv || !argv[0] || !argv[1] || !argv[2] || !*argv[1] || !*argv[2]) return 1;
    if (argc == 3 && strcasecmp(argv[1], "DETENTS") == 0) {
        uint32_t steps;
        if (!ParseEncoderUnsigned(argv[2], 1, UINT8_MAX, &steps)) {
            LOC_PRINTF(catalog, LOC_CLI_ENCODER_INVALID_DETENT_COUNT_EXPECTED_1_255, "Invalid detent count; expected 1-255.\n");
            return 1;
        }
        if (!rotaryEncoderService.base.SetStepsPerDetent) return 1;
        esp_err_t result = rotaryEncoderService.base.SetStepsPerDetent(&rotaryEncoderService.base, (uint8_t)steps);
        if (result != ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_ENCODER_DETENT_CONFIGURATION_FAILED, "Detent configuration failed: 0x%x\n", result);
            return 1;
        }
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_STEPS_PER_DETENT_SET_TO, "Steps per detent set to %u\n", (unsigned)steps);
        return 0;
    }

    if (argc == 3 && strcasecmp(argv[1], "INVERT") == 0) {
        bool inverted;
        if (!ParseEncoderBoolean(argv[2], &inverted)) {
            LOC_PRINTF(catalog, LOC_CLI_ENCODER_INVALID_INVERSION_VALUE_EXPECTED_ON_OFF_TRUE_FALSE_OR_1_0, "Invalid inversion value; expected ON/OFF, TRUE/FALSE or 1/0.\n");
            return 1;
        }
        if (!rotaryEncoderService.base.SetDirectionInverted || rotaryEncoderService.base.SetDirectionInverted(&rotaryEncoderService.base, inverted) != ESP_OK) return 1;
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_DIRECTION_INVERSION_SET_TO, "Direction inversion set to %s\n", inverted ? Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_ON) : Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_OFF));
        return 0;
    }

    LOC_PRINTF(catalog, LOC_CLI_ENCODER_USAGE_DETAILS, "Usage:\n");
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ENCCONFIG_DETENTS_1_255, "  ENCCONFIG DETENTS <1-255>\n");
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ENCCONFIG_INVERT_ON_OFF, "  ENCCONFIG INVERT <ON|OFF>\n");
    return 1;
}

/* ── ENCSIM: Simulate Encoder Input ──────────────────────────────────── */

static int cmd_enc_sim(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc != 2 && argc != 3) return 1;
    if (!argv || !argv[0] || !argv[1] || !*argv[1] || (argc==3 && !argv[2])) return 1;
    const char *names[] = {"CW", "CCW", "CLICK", "DBL", "LONG", "HELD", "RELEASE"};
    RotaryEncoderSimulation simulation = RE_SIM_NONE;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (!strcasecmp(argv[1], names[i])) simulation = (RotaryEncoderSimulation)(RE_SIM_CW + i);
    }
    uint32_t count = 1;
    if (simulation == RE_SIM_NONE ||
        (argc == 3 && (simulation >= RE_SIM_CLICK || !ParseEncoderUnsigned(argv[2], 1, INT16_MAX, &count)))) {
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_USAGE_DETAILS_USAGE_ENCSIM_CW_CCW_1_32767_OR_CLICK_DBL_LONG_HELD_RELEASE, "Usage: ENCSIM <CW|CCW> [1-32767] or <CLICK|DBL|LONG|HELD|RELEASE>\n");
        return 1;
    }
    esp_err_t result = RotaryEncoder_Simulate(simulation, (uint16_t)count);
    if (result != ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_SIMULATION_FAILED, "Simulation failed: 0x%x\n", result);
        return 1;
    }
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_DIAGNOSTIC_EVENT_QUEUED_BY_ENCODER_WORKER_NOT_PHYSICAL_INPUT_EVIDENCE, "Diagnostic event queued by encoder worker; not physical input evidence.\n");
    return 0;
}

/* ── ENCACCEL: Acceleration Config ───────────────────────────────────── */

static int cmd_enc_accel(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc<1 || argc>3 || !argv) return 1;
    for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return 1;
    if (argc == 2 || argc == 3) {
        bool enabled;
        uint32_t threshold = RE_ACCEL_THRESHOLD_MS;
        if (!ParseEncoderBoolean(argv[1], &enabled) ||
            (argc == 3 && !ParseEncoderUnsigned(argv[2], 0, UINT8_MAX, &threshold))) {
            LOC_PRINTF(catalog, LOC_CLI_ENCODER_USAGE_DETAILS_USAGE_ENCACCEL_ON_OFF_0_255_MS, "Usage: ENCACCEL <ON|OFF> [0-255 ms]\n");
            return 1;
        }
        if (!rotaryEncoderService.base.SetAcceleration || rotaryEncoderService.base.SetAcceleration(&rotaryEncoderService.base,
                                                       enabled, (uint8_t)threshold) != ESP_OK) return 1;
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_ACCELERATION_DETAILS, "Acceleration %s (threshold=%u ms)\n",
               enabled ? Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_ENABLED) : Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_DISABLED), (unsigned)threshold);
        return 0;
    }

    if (argc != 1) return 1;
    RotaryEncoderRuntimeConfig config;
    if (!rotaryEncoderService.base.GetRuntimeConfig || rotaryEncoderService.base.GetRuntimeConfig(&rotaryEncoderService.base, &config)!=ESP_OK) return 1;
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ACCELERATION_DETAILS_ACCELERATION_S_THRESHOLD_U_MS_MAX_U, "Acceleration: %s threshold=%u ms max=%u\n",
           config.accel_enabled ? Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_ON) : Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_OFF),
           config.accel_threshold_ms,
           (unsigned)RE_ACCEL_MAX_MULTIPLIER);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_USAGE_DETAILS_USAGE_ENCACCEL_ON_OFF_THRESHOLD_MS, "Usage: ENCACCEL <ON|OFF> [threshold_ms]\n");
    return 0;
}

/* ── ENCDEBOUNCE: Debounce Config ────────────────────────────────────── */

static int cmd_enc_debounce(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc<1 || argc>3 || !argv) return 1;
    for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return 1;
    if (argc == 3) {
        uint32_t rot_ms, btn_ms;
        if (!ParseEncoderUnsigned(argv[1], 0, UINT16_MAX, &rot_ms) ||
            !ParseEncoderUnsigned(argv[2], 0, UINT16_MAX, &btn_ms)) {
            LOC_PRINTF(catalog, LOC_CLI_ENCODER_USAGE_DETAILS_USAGE_ENCDEBOUNCE_0_65535_MS_0_65535_MS, "Usage: ENCDEBOUNCE <0-65535 ms> <0-65535 ms>\n");
            return 1;
        }
        if (!rotaryEncoderService.base.SetDebounceMs || rotaryEncoderService.base.SetDebounceMs(&rotaryEncoderService.base, (uint16_t)rot_ms, (uint16_t)btn_ms) != ESP_OK) return 1;
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_DEBOUNCE_SET, "Debounce set: rotation=%u ms, button=%u ms\n", (unsigned)rot_ms, (unsigned)btn_ms);
        return 0;
    }

    if (argc != 1) return 1;
    RotaryEncoderRuntimeConfig config;
    if (!rotaryEncoderService.base.GetRuntimeConfig || rotaryEncoderService.base.GetRuntimeConfig(&rotaryEncoderService.base, &config)!=ESP_OK) return 1;
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_DEBOUNCE, "Debounce: rotation=%u ms, button=%u ms\n",
           config.debounce_rotation_ms, config.debounce_button_ms);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_USAGE_DETAILS_USAGE_ENCDEBOUNCE_ROTATION_MS_BUTTON_MS, "Usage: ENCDEBOUNCE <rotation_ms> <button_ms>\n");
    return 0;
}

/* ── ENCDIAG: Detailed Diagnostics ───────────────────────────────────── */

static int cmd_enc_diag(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc != 1 || !argv || !argv[0] || !*argv[0] ||
        !rotaryEncoderService.base.GetStatus || !rotaryEncoderService.base.GetDiagnostics) return 1;

    RotaryEncoderDiagnostics diag;
    if (rotaryEncoderService.base.GetDiagnostics(&rotaryEncoderService.base, &diag) != ESP_OK) return 1;
    RotaryEncoderStatus status = rotaryEncoderService.base.GetStatus(&rotaryEncoderService.base);

    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ENCODER_DIAGNOSTICS, "\n--- Encoder Diagnostics ---\n");
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ISR_COUNT_DIAGNOSTIC, "ISR Count:        %lu\n", (unsigned long)diag.isr_count);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ROTATION_EVENTS_DETAILS, "Rotation Events:  %lu\n", (unsigned long)diag.rotation_events);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_BUTTON_EVENTS_DIAGNOSTIC, "Button Events:    %lu\n", (unsigned long)diag.button_events);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_QUEUE_OVERFLOWS_DETAILS, "Queue Overflows:  %lu\n", (unsigned long)diag.queue_overflows);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_PCNT_OVERFLOWS_DETAILS, "PCNT Overflows:   %lu\n", (unsigned long)diag.pcnt_overflow_count);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_CALLBACK_FAILS, "Callback Fails:   %lu\n", (unsigned long)diag.callback_failures);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_DROPPED_ROT, "Dropped Rot:      %lu\n", (unsigned long)status.dropped_rotation_events);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_DROPPED_BTN, "Dropped Btn:      %lu\n", (unsigned long)status.dropped_button_events);
    if (diag.last_activity_ms > 0) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_LAST_ACTIVITY_DIAGNOSTIC, "Last Activity:    %lu ms ago\n", (unsigned long)(now - diag.last_activity_ms));
    } else {
        LOC_PRINTF(catalog, LOC_CLI_ENCODER_LAST_ACTIVITY_DIAGNOSTIC_LAST_ACTIVITY_NEVER, "Last Activity:    Never\n");
    }
    portENTER_CRITICAL(&rotaryEncoderService.spinlock);
    int32_t pending = rotaryEncoderService.pcnt_pending_detents;
    portEXIT_CRITICAL(&rotaryEncoderService.spinlock);
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_PENDING_DETENTS, "Pending Detents:  %ld\n", (long)pending);
    printf("---------------------------\n");
    return 0;
}

/* ── ENCTEST: Self-Test ──────────────────────────────────────────────── */

static int cmd_enc_test(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    (void)argv;
    if (argc != 1) return 1;
    IService *service = &rotaryEncoderService.base.base;
    bool healthy = service->CheckHealth && service->CheckHealth(service) == 0;
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_ENCODER_LIFECYCLE_HEALTH, "Encoder lifecycle health: %s\n", healthy ? Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_PASS) : Localization_Text(catalog, LOC_CLI_ENCODER_VALUE_FAIL));
    LOC_PRINTF(catalog, LOC_CLI_ENCODER_READ_ONLY_CHECK_PULSE_DECODING_AND_GESTURE_RECOGNITION_REQUIRE_THEIR_SEPARATE_TESTS, "Read-only check; pulse decoding and gesture recognition require their separate tests.\n");
    return healthy ? 0 : 1;
}

/* Machine acknowledgement reports checked handler completion, not a physical
 * detent or queued simulation being executed. Status must verify runtime state. */
static int Encoder_DispatchMachine(int argc, char **argv, int (*handler)(int,char **),
                                   const char *command) {
    if (argc<1 || argc>5 || !argv) return 1;
    for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return 1;
    bool machine=argc>=2 && !strcmp(argv[argc-1],"--machine");
    int result=handler(argc-(machine ? 1 : 0),argv);
    if (machine) printf("RACOOL_CLI v=1 type=encoder_command command=%s result=%s error=%d\n",
        command,result==0 ? "completed" : "rejected",result);
    return result;
}
static int cmd_enc_save_machine(int argc,char **argv) { return Encoder_DispatchMachine(argc,argv,cmd_enc_save,"ENCSAVE"); }
static int cmd_enc_reset_machine(int argc,char **argv) { return Encoder_DispatchMachine(argc,argv,cmd_enc_reset,"ENCRESET"); }
static int cmd_enc_config_machine(int argc,char **argv) { return Encoder_DispatchMachine(argc,argv,cmd_enc_config,"ENCCONFIG"); }
static int cmd_enc_sim_machine(int argc,char **argv) { return Encoder_DispatchMachine(argc,argv,cmd_enc_sim,"ENCSIM"); }
static int cmd_enc_accel_machine(int argc,char **argv) { return Encoder_DispatchMachine(argc,argv,cmd_enc_accel,"ENCACCEL"); }
static int cmd_enc_debounce_machine(int argc,char **argv) { return Encoder_DispatchMachine(argc,argv,cmd_enc_debounce,"ENCDEBOUNCE"); }
static int cmd_enc_diag_machine(int argc,char **argv) { return Encoder_DispatchMachine(argc,argv,cmd_enc_diag,"ENCDIAG"); }
static int cmd_enc_test_machine(int argc,char **argv) { return Encoder_DispatchMachine(argc,argv,cmd_enc_test,"ENCTEST"); }

/* ── Command Registration ────────────────────────────────────────────── */

static IDebugConsole *encoder_command_owner;
static bool encoder_command_registered[9];

esp_err_t register_encoder_commands(IDebugConsole *console) {
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_ARG;
    if (encoder_command_owner && encoder_command_owner != console) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t enc_cmds[] = {
        {.command="ENCSAVE", .help="Persist current encoder detent, acceleration and direction for next boot", .func=cmd_enc_save_machine},

        {
            .command = "ENC",
            .help    = "Show rotary encoder status",
            .func    = &cmd_enc_info,
            .hint    = NULL,
        },
        {
            .command = "ENCRESET",
            .help    = "Reset rotary encoder counters and position",
            .func    = &cmd_enc_reset_machine,
            .hint    = NULL,
        },
        {
            .command = "ENCCONFIG",
            .help    = "Configure encoder runtime parameters",
            .func    = &cmd_enc_config_machine,
            .hint    = "<DETENTS n|INVERT ON/OFF>",
        },
        {
            .command = "ENCSIM",
            .help    = "Simulate encoder input events",
            .func    = &cmd_enc_sim_machine,
            .hint    = "<CW|CCW|CLICK|DBL|LONG|HELD|RELEASE> [count]",
        },
        {
            .command = "ENCACCEL",
            .help    = "View or configure rotation acceleration",
            .func    = &cmd_enc_accel_machine,
            .hint    = "[ON|OFF] [threshold_ms]",
        },
        {
            .command = "ENCDEBOUNCE",
            .help    = "View or configure debounce timing",
            .func    = &cmd_enc_debounce_machine,
            .hint    = "[rotation_ms] [button_ms]",
        },
        {
            .command = "ENCDIAG",
            .help    = "Show detailed encoder diagnostics",
            .func    = &cmd_enc_diag_machine,
            .hint    = NULL,
        },
        {
            .command = "ENCTEST",
            .help    = "Check encoder lifecycle health without simulated input",
            .func    = &cmd_enc_test_machine,
            .hint    = NULL,
        },
    };

    _Static_assert(sizeof(encoder_command_registered) / sizeof(encoder_command_registered[0]) ==
                   sizeof(enc_cmds) / sizeof(enc_cmds[0]), "Encoder registration state must cover every command");
    for (size_t i = 0; i < sizeof(enc_cmds) / sizeof(enc_cmds[0]); i++) {
        if (encoder_command_registered[i]) continue;
        int32_t result = console->RegisterCommandWithCategory(console, &enc_cmds[i], CMD_CAT_HARDWARE);
        if (result != 0) return (esp_err_t)result;
        encoder_command_owner = console;
        encoder_command_registered[i] = true;
    }
    return ESP_OK;
}
