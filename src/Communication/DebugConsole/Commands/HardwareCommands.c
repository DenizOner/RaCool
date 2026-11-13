#include "Core/Localization/Localization.h"
#include "Controllers/ClimateController/FanCommands.h"
#include <stdlib.h>
#include <limits.h>
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorInput.h"
#include "Hardware/BuiltinLED/BuiltinLEDInput.h"
#include "Communication/DebugConsole/Commands/HardwareCommands.h"
#include <string.h>
#include <stdio.h>
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include "Hardware/InfraredTemperatureSensor/IInfraredTemperatureSensor.h"
#include "Hardware/Relay/IRelay.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include <esp_flash.h>
#include "esp_chip_info.h"
#include "Hardware/OLED/IOLED.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/RotaryEncoder/IRotaryEncoder.h"
#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/I2CManager/II2CManager.h"
#include <driver/gpio.h>
#include <esp_timer.h>

static int do_hw_summary(int argc, char **argv) {
    if (argc != 1) return 1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) return 1;

    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_001, "\n--- Hardware Summary ---\n");
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_002, "Chip: ESP32-C6 (Rev %d)\n", chip_info.revision);
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_003, "Cores: %d\n", chip_info.cores);
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_004, "Flash: %lu MB (%s)\n", (unsigned long)(flash_size / (1024 * 1024)), 
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_EMBEDDED) : Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_EXTERNAL));
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_005, "Features: %s%s%s%s\n",
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? "Zigbee/Thread " : "");

    return 0;
}

static int do_temp(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    float offset = 0.0f;long interval=0,minimum=0,maximum=0;
    bool valid = argc == 1;
    if (argc == 2) valid = !strcasecmp(argv[1],"STATS") || !strcasecmp(argv[1],"RESET") || !strcasecmp(argv[1],"TEST");
    if (argc == 3 && !strcasecmp(argv[1],"OFFSET")) valid = InternalTemp_ParseFloat(argv[2],&offset);
    if (argc == 3 && !strcasecmp(argv[1],"POLL")) valid = InternalTemp_ParseInteger(argv[2],CPU_POLL_MIN_MS,CPU_POLL_MAX_MS,&interval);
    if (argc == 4 && !strcasecmp(argv[1],"LIMITS")) valid = InternalTemp_ParseInteger(argv[2],INT16_MIN,INT16_MAX,&minimum) && InternalTemp_ParseInteger(argv[3],INT16_MIN,INT16_MAX,&maximum) && minimum < maximum;
    if (!valid) {LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_006, "Usage: HW TEMP [STATS|RESET|TEST|OFFSET <finite_C>|POLL <ms>|LIMITS <min_C> <max_C>]\n");return 1;}

    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_007, "\n--- Temperature Sensors ---\n");
    
    // CPU Temp
    IInternalTemperatureSensor *internal = (IInternalTemperatureSensor *)ServiceLocator_GetService(SVC_NAME_INTERNAL_TEMP_SENSOR);
    if (!internal && argc > 1) return 1;
    if (internal) {
        if (argc > 1 && strcasecmp(argv[1], "STATS") == 0) {
            ThermalStats stats;
            if (internal->GetStats && internal->GetLastTemperature && internal->GetStats(internal, &stats) == 0) {
                LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_008, "CPU Temp Stats: Current=%.2f C, EMA Avg=%.2f C, Min=%.2f C, Max=%.2f C, Rate=%.2f C/min (Samples: %lu)\n",
                       internal->GetLastTemperature(internal), stats.avg_temp, stats.min_temp, stats.max_temp, 
                       stats.rate_of_change_c_per_min, (unsigned long)stats.total_samples);
            } else {
                LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_009, "CPU temperature statistics unavailable.\n");
                return 1;
            }
        } else if (argc > 2 && strcasecmp(argv[1], "OFFSET") == 0) {
            if (!internal->SetOffset || internal->SetOffset(internal,offset) != 0) return 1;
            {
                LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_010, "CPU Temp offset set to %.2f C\n", offset);
            }
        } else if (argc > 1 && strcasecmp(argv[1], "RESET") == 0) {
            if (!internal->ResetStats) return 1;
            if (internal->ResetStats) {
                if (internal->ResetStats(internal) != 0) return 1;
                LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_011, "CPU Temp statistics reset.\n");
            }
        } else if (argc > 1 && strcasecmp(argv[1], "TEST") == 0) {
            if (!internal->RunSelfTest) return 1;
            if (internal->RunSelfTest) {
                int res = internal->RunSelfTest(internal);
                if (res == 0) {
                    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_012, "CPU Temp Self-Test PASSED.\n");
                } else {
                    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_013, "CPU Temp Self-Test FAILED.\n");
                    return 1;
                }
            }
        } else if (argc > 2 && strcasecmp(argv[1], "POLL") == 0) {
            if (!internal->StartPolling) return 1;
            if (internal->StartPolling) {
                if (internal->StartPolling(internal, interval) == 0) {
                    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_014, "CPU Temp polling interval set to %lu ms.\n", (unsigned long)interval);
                } else {
                    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_015, "Failed to update polling interval.\n");
                    return 1;
                }
            }
        } else if (argc > 3 && strcasecmp(argv[1], "LIMITS") == 0) {
            int16_t r_min = (int16_t)minimum;
            int16_t r_max = (int16_t)maximum;
            if (!internal->SetLimits) return 1;
            if (internal->SetLimits) {
                if (internal->SetLimits(internal, r_min, r_max) == 0) {
                    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_016, "CPU Temp range limits updated: [%dÃ‚Â°C to %dÃ‚Â°C]\n", r_min, r_max);
                } else {
                    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_017, "Failed to update range limits.\n");
                    return 1;
                }
            }
        } else {
            if (!internal->ReadTemperature) return 1;
            float cpu_temp = internal->ReadTemperature(internal);
            if (!isfinite(cpu_temp)) return 1;
            ThermalLevel level = internal->GetThermalLevel ? internal->GetThermalLevel(internal) : THERMAL_LEVEL_NORMAL;
            ThermalTrend trend = internal->GetThermalTrend ? internal->GetThermalTrend(internal) : THERMAL_TREND_STABLE;
            
            const char* level_str = (level == THERMAL_LEVEL_EMERGENCY) ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_EMERGENCY) :
                                    (level == THERMAL_LEVEL_CRITICAL) ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_CRITICAL) :
                                    (level == THERMAL_LEVEL_WARNING) ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_WARNING) :
                                    internal->GetThermalLevel ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_NORMAL) : Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_UNKNOWN);
                                    
            const char* trend_str = (trend == THERMAL_TREND_RAPID_RISE) ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_RAPID_RISE) :
                                    (trend == THERMAL_TREND_RISING) ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_RISING) :
                                    (trend == THERMAL_TREND_FALLING) ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_FALLING) :
                                    internal->GetThermalTrend ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_STABLE) : Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_UNKNOWN);
                                    
            LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_018, "CPU Temp: %.2f C [Thermal Level: %s, Trend: %s]\n", cpu_temp, level_str, trend_str);
        }
    } else {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_019, "CPU Temp: Not Found\n");
    }

    // IR Temp
    IInfraredTemperatureSensor *ir = (IInfraredTemperatureSensor *)ServiceLocator_GetService(SVC_NAME_IR_TEMP_SENSOR);
    if (ir) {
        if (!ir->ReadObjectTemperature || !ir->ReadAmbientTemperature) return 1;
        float obj = ir->ReadObjectTemperature(ir);
        float amb = ir->ReadAmbientTemperature(ir);
        if (!isfinite(obj) || !isfinite(amb)) return 1;
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_020, "Radiator Temp: %.2f C\n", obj);
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_021, "Ambient Temp: %.2f C\n", amb);
    } else {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_022, "IR Temp: Not Found\n");
    }

    return 0;
}



static int do_gpio_info(int argc, char **argv) {
    if (argc != 1) return 1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_023, "\n--- Physical GPIO Levels ---\n");
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_024, "%-8s %-8s\n", "GPIO", "Level");
    for (int i = 0; i <= 22; i++) {
        if (GPIO_IS_VALID_GPIO(i)) {
            LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_025, "GPIO %-3d: %d\n", i, gpio_get_level((gpio_num_t)i));
        }
    }
    for (int i = 25; i <= 30; i++) {
        if (GPIO_IS_VALID_GPIO(i)) {
            LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_026, "GPIO %-3d: %d\n", i, gpio_get_level((gpio_num_t)i));
        }
    }
    return 0;
}

static int do_hw_sim(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 2) return 1;
    RotaryEncoderSimulation simulation;
    if (!strcasecmp(argv[1], "CLICK")) simulation = RE_SIM_CLICK;
    else if (!strcasecmp(argv[1], "CW")) simulation = RE_SIM_CW;
    else if (!strcasecmp(argv[1], "CCW")) simulation = RE_SIM_CCW;
    else return 1;
    esp_err_t result = RotaryEncoder_Simulate(simulation, 1);
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_027, "Encoder simulation admission: %d\n", (int)result);
    return result == ESP_OK ? 0 : 1;
}

static int do_relay_status(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    IRelay *relay = (IRelay *)ServiceLocator_GetService(SVC_NAME_RELAY);
    if (!relay) {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_028, "Relay: Not Found\n");
        return 1;
    }

    if (argc > 1) {
        if (argc!=2) return 1;
        ClimateFanMode mode;
        if (!strcasecmp(argv[1],"ON")) mode=CLIMATE_FAN_MANUAL_ON;
        else if (!strcasecmp(argv[1],"OFF")) mode=CLIMATE_FAN_MANUAL_OFF;
        else return 1;
        if (FanCommands_Dispatch(ConfigurationManager_GetInstance(),(FanCommand){FAN_COMMAND_MODE,mode,0},false)!=ESP_OK) return 1;
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_029, "Fan setting accepted; read CLIMATE STATUS for applied output.\n");
    }

    if (!relay->IsOpen) return 1;
    bool state = relay->IsOpen(relay);
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_030, "Relay Status: %s\n", state ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_OPEN) : Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_CLOSED));
    return 0;
}

static int do_oled_info(int argc, char **argv) {
    if (argc != 1) return 1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    IOLED *oled = (IOLED *)ServiceLocator_GetService(SVC_NAME_OLED);
    if (oled) {
        if (!oled->base.GetState) return 1;
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_031, "\n--- OLED Display Information ---\n");
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_032, "State: %s\n", oled->base.GetState(&oled->base) == SERVICE_STATE_STARTED ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_ACTIVE) : Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_OFF));
        // Internal module details are private; report the public status here.
    } else {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_033, "OLED: Not found\n");
    }
    return 0;
}

static int do_pin_map(int argc, char **argv) {
    if (argc != 1) return 1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    IPinManager *pinMgr = (IPinManager *)ServiceLocator_GetService(SVC_NAME_PIN_MANAGER);
    if (pinMgr) {
        if (!pinMgr->DumpPinMap) return 1;
        return pinMgr->DumpPinMap(pinMgr) == 0 ? 0 : 1;
    } else {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_034, "PinManager not found\n");
    }
    return 0;
}
static int do_enc_info(int argc, char **argv) {
    if (argc != 1) return 1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    IRotaryEncoder *enc = (IRotaryEncoder *)ServiceLocator_GetService(SVC_NAME_ROTARY_ENCODER);
    if (enc) {
        if (!enc->GetStatus || !enc->GetDiagnostics) return 1;
        RotaryEncoderStatus s = enc->GetStatus(enc);
        RotaryEncoderDiagnostics diag;
        memset(&diag, 0, sizeof(diag));
        if (enc->GetDiagnostics(enc, &diag) != ESP_OK) return 1;
        
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_035, "\n--- Encoder Status ---\n");
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_036, "Position: %ld\n", (long)s.absolute_position);
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_037, "Button: %s\n", s.button_held ? Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_HELD) : Localization_Text(catalog, LOC_CLI_HARDWARE_VALUE_RELEASED));
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_038, "Total Rotations: %lu\n", (unsigned long)s.total_rotations);
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_039, "Total Clicks: %lu\n", (unsigned long)s.total_clicks);
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_040, "ISR Count: %lu\n", (unsigned long)diag.isr_count);
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_041, "Queue Overflows: %lu\n", (unsigned long)diag.queue_overflows);
    } else {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_042, "Encoder: Not Found\n");
    }
    return 0;
}

static int do_rgb_ctrl(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    IRGBLED *rgb = (IRGBLED *)ServiceLocator_GetService(SVC_NAME_RGB_LED);
    if (!rgb || !rgb->TurnOff || argc != 2 || strcasecmp(argv[1], "OFF") != 0) return 1;
    esp_err_t result = rgb->TurnOff(rgb);
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_043, "RGB off queue result: %d; worker application is asynchronous.\n", (int)result);
    return result == ESP_OK ? 0 : 1;
}

static int do_led_ctrl(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    IBuiltinLED *led = (IBuiltinLED *)ServiceLocator_GetService(SVC_NAME_BUILTIN_LED);
    if (!led) {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_044, "BuiltinLED service not found\n");
        return 1;
    }

    if (argc < 2) {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_045, "Usage: HW LED <ON|OFF|STOP|ANIM> [param]\n");
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_046, "Example: HW LED ANIM 0 (Heartbeat)\n");
        return 1;
    }

    if (strcasecmp(argv[1], "ON") == 0 || strcasecmp(argv[1], "OFF") == 0) {
        if (argc != 2) return 1;
        bool enabled = strcasecmp(argv[1], "ON") == 0;
        esp_err_t err = BuiltinLED_SaveEnabled(enabled);
        if (err == ESP_OK) { LOC_PRINTF(catalog, LOC_CLI_HARDWARE_LED_SAVED, "LED mode saved (0x%x).\n", err); }
        else { LOC_PRINTF(catalog, LOC_CLI_HARDWARE_LED_SAVE_FAILED, "LED mode save failed: 0x%x\n", err); }
        return err == ESP_OK ? 0 : 1;
    } else if (strcasecmp(argv[1], "STOP") == 0) {
        if (argc != 2 || !led->StopAnimation) return 1;
        esp_err_t result = led->StopAnimation(led);
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_047, "LED stop queue result: %d\n", (int)result);
        return result == ESP_OK ? 0 : 1;
    } else if (strcasecmp(argv[1], "ANIM") == 0) {
        if (argc < 3 || argc > 4 || !led->SetAnimation) {
            LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_048, "Specify an animation ID from builtin_led_list\n");
            return 1;
        }
        uint32_t anim_id, duration = 0;
        if (!BuiltinLEDInput_ParseUnsigned(argv[2], BUILTIN_LED_ANIM_COUNT - 1, &anim_id) ||
            (argc == 4 && !BuiltinLEDInput_ParseUnsigned(argv[3], UINT32_MAX, &duration))) return 1;
        
        esp_err_t err = led->SetAnimation(led, (BuiltinLEDAnimationType)anim_id, BUILTIN_LED_PRIO_HIGH, duration);
        if (err == ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_049, "Animasyon %d baslatildi (SÃƒÂ¼re: %lu ms).\n", (int)anim_id, (unsigned long)duration);
        } else {
            LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_050, "Animation queue rejected: 0x%x\n", err);
            return 1;
        }
    } else {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_051, "Invalid LED command: %s\n", argv[1]);
        return 1;
    }

    return 0;
}

static int do_hw_scan(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    uint32_t selected = 0;
    if (argc > 2 || (argc == 2 && !BuiltinLEDInput_ParseUnsigned(argv[1], I2C_NUM_MAX - 1, &selected))) return 1;
    II2CManager *i2c = (II2CManager *)ServiceLocator_GetService(SVC_NAME_I2C_MANAGER);
    if (!i2c || !i2c->Scan) return 1;
    IOLED *oled = (IOLED *)ServiceLocator_GetService(SVC_NAME_OLED);
    IInfraredTemperatureSensor *ir = (IInfraredTemperatureSensor *)ServiceLocator_GetService(SVC_NAME_IR_TEMP_SENSOR);
    bool restart_ir = false;
    if (ir) {
        if (!ir->base.GetState || !ir->base.Stop || !ir->base.Start) return 1;
        ServiceState state = ir->base.GetState(&ir->base);
        if (state != SERVICE_STATE_STARTED && state != SERVICE_STATE_STOPPED) return 1;
        restart_ir = state == SERVICE_STATE_STARTED;
    }
    if (oled && (!oled->Lock || !oled->Unlock || !oled->Lock(oled))) return 1;
    int result = 1;
    bool ir_stopped = false;
    if (restart_ir) {
        if (ir->base.Stop(&ir->base) != 0) goto cleanup;
        ir_stopped = true;
    }
    result = i2c->Scan(i2c, (i2c_port_t)selected) == ESP_OK ? 0 : 1;
cleanup:
    if (ir_stopped && ir->base.Start(&ir->base) != 0) result = 1;
    if (oled) oled->Unlock(oled);
    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_052, "I2C scan and restoration result: %d\n", result);
    return result;
}

static int dispatch_hardware(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 7)) return 1;
    if (argc < 2) {
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_053, "Usage: HW <subcommand>\n");
        LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_054, "Subcommands: SUMMARY, TEMP, GPIO, PINS, RELAY, OLED, ENC, RGB, LED, SCAN, SIM\n");
        return 1;
    }

    if (strcasecmp(argv[1], "SUMMARY") == 0) return do_hw_summary(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "TEMP") == 0) return do_temp(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "GPIO") == 0) return do_gpio_info(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "PINS") == 0) return do_pin_map(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "RELAY") == 0) return do_relay_status(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "OLED") == 0) return do_oled_info(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "ENC") == 0) return do_enc_info(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "RGB") == 0) return do_rgb_ctrl(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "LED") == 0) return do_led_ctrl(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "SCAN") == 0) return do_hw_scan(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "SIM") == 0) return do_hw_sim(argc - 1, &argv[1]);

    LOC_PRINTF(catalog, LOC_CLI_HARDWARE_MESSAGE_055, "Unknown subcommand: %s\n", argv[1]);
    return 1;
}

static int cmd_hardware(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 8)) return 1;
    bool machine = strcmp(argv[argc - 1], "--machine") == 0;
    const char *canonical = NULL;
    static const char *const names[] = {"SUMMARY", "TEMP", "GPIO", "PINS", "RELAY", "OLED", "ENC", "RGB", "LED", "SCAN", "SIM"};
    if (argc >= 2)
        for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
            if (!strcasecmp(argv[1], names[index])) canonical = names[index];
    if (machine && !canonical) return 1;
    int result = dispatch_hardware(argc - (machine ? 1 : 0), argv);
    if (machine) printf("RACOOL_CLI v=1 type=hardware_command command=%s result=%s\n",
                        canonical, result == 0 ? "accepted" : "rejected");
    return result;
}

void HardwareCommands_Register(IDebugConsole *console) {
    static const esp_console_cmd_t hw_cmds[] = {
        {
            .command = "HARDWARE",
            .help = "Donanim yonetim komutlari",
            .func = &cmd_hardware,
            .hint = "<subcmd>"
        },
        {
            .command = "HW",
            .help = "Donanim yonetim komutlari (kisa)",
            .func = &cmd_hardware,
            .hint = "<subcmd>"
        }
    };

    for (int i = 0; i < sizeof(hw_cmds) / sizeof(hw_cmds[0]); i++) {
        console->RegisterCommandWithCategory(console, &hw_cmds[i], CMD_CAT_HARDWARE);
    }
}
