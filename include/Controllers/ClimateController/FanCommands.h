#ifndef RACOOL_FAN_COMMANDS_H
#define RACOOL_FAN_COMMANDS_H
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Controllers/ClimateController/ClimateControllerConfig.h"
#include <math.h>

typedef enum {
    FAN_COMMAND_MODE, FAN_COMMAND_OBJECT_SOURCE, FAN_COMMAND_AMBIENT_SOURCE,
    FAN_COMMAND_OBJECT_ON, FAN_COMMAND_OBJECT_OFF,
    FAN_COMMAND_DELTA_ON, FAN_COMMAND_DELTA_OFF, FAN_COMMAND_DELTA_PAIR, FAN_COMMAND_RESET
} FanCommandType;
typedef struct { FanCommandType type; float value; float second_value; } FanCommand;

/* Shared semantic dispatch; configuration owner validates the complete candidate
 * under its mutex and owns persistence/rollback/notification. No UI owns state. */
static inline esp_err_t FanCommands_Dispatch(IConfigurationManager *owner, FanCommand command, bool persist) {
    if (!owner || !owner->UpdateFields) return ESP_ERR_INVALID_STATE;
    if (!isfinite(command.value)) return ESP_ERR_INVALID_ARG;
    ConfigurationFieldUpdate fields[9];
    size_t count = 1;
    uint8_t selection = 0, strategy = 0;
    uint8_t default_object_source=DEFAULT_OBJECT_TEMPERATURE_SOURCE;
    uint8_t default_ambient_source=DEFAULT_AMBIENT_TEMPERATURE_SOURCE;
    bool manual = false, state = false;
    int16_t delta = 0, second_delta = 0;
    float default_on=DEFAULT_RELAY_OPEN_TEMP, default_off=DEFAULT_RELAY_CLOSE_TEMP;
    switch (command.type) {
    case FAN_COMMAND_MODE:
        if (command.value < 0 || command.value > 3 || floorf(command.value) != command.value) return ESP_ERR_INVALID_ARG;
        manual = command.value == CLIMATE_FAN_MANUAL_ON || command.value == CLIMATE_FAN_MANUAL_OFF;
        state = command.value == CLIMATE_FAN_MANUAL_ON;
        strategy = command.value == CLIMATE_FAN_AMBIENT_DELTA ? 1 : 0;
        fields[0] = (ConfigurationFieldUpdate){offsetof(ControlConfigs, fan_manual_mode), &manual, sizeof(manual)};
        fields[1] = (ConfigurationFieldUpdate){offsetof(ControlConfigs, fan_manual_state), &state, sizeof(state)};
        fields[2] = (ConfigurationFieldUpdate){offsetof(ControlConfigs, fan_strategy), &strategy, sizeof(strategy)};
        count = 3; break;
    case FAN_COMMAND_OBJECT_SOURCE: case FAN_COMMAND_AMBIENT_SOURCE:
        if (command.value < 0 || command.value >= TEMPERATURE_SOURCE_COUNT || floorf(command.value) != command.value) return ESP_ERR_INVALID_ARG;
        selection = (uint8_t)command.value;
        fields[0] = (ConfigurationFieldUpdate){command.type == FAN_COMMAND_OBJECT_SOURCE ?
            offsetof(ControlConfigs, object_temperature_source) : offsetof(ControlConfigs, ambient_temperature_source), &selection, sizeof(selection)};
        break;
    case FAN_COMMAND_OBJECT_ON: case FAN_COMMAND_OBJECT_OFF:
        fields[0] = (ConfigurationFieldUpdate){command.type == FAN_COMMAND_OBJECT_ON ?
            offsetof(ControlConfigs, relay_open_temp) : offsetof(ControlConfigs, relay_close_temp), &command.value, sizeof(command.value)};
        break;
    case FAN_COMMAND_DELTA_ON: case FAN_COMMAND_DELTA_OFF: case FAN_COMMAND_DELTA_PAIR:
        if (command.value < FAN_DELTA_MIN_DECIC/10.0f || command.value > FAN_DELTA_MAX_DECIC/10.0f || fabsf(command.value * 10 - roundf(command.value * 10)) > 0.0001f) return ESP_ERR_INVALID_ARG;
        delta = (int16_t)lroundf(command.value * 10);
        fields[0] = (ConfigurationFieldUpdate){command.type == FAN_COMMAND_DELTA_OFF ?
            offsetof(ControlConfigs, fan_delta_off_decic) : offsetof(ControlConfigs, fan_delta_on_decic), &delta, sizeof(delta)};
        if (command.type == FAN_COMMAND_DELTA_PAIR) {
            if (!isfinite(command.second_value) || command.second_value < FAN_DELTA_MIN_DECIC/10.0f || command.second_value > FAN_DELTA_MAX_DECIC/10.0f ||
                fabsf(command.second_value * 10 - roundf(command.second_value * 10)) > 0.0001f) return ESP_ERR_INVALID_ARG;
            second_delta = (int16_t)lroundf(command.second_value * 10);
            fields[1] = (ConfigurationFieldUpdate){offsetof(ControlConfigs, fan_delta_off_decic), &second_delta, sizeof(second_delta)};
            count = 2;
        }
        break;
    case FAN_COMMAND_RESET:
        strategy=DEFAULT_FAN_STRATEGY;
        manual=DEFAULT_FAN_MANUAL_MODE;state=DEFAULT_FAN_MANUAL_STATE;
        delta=DEFAULT_FAN_DELTA_ON_DECIC;second_delta=DEFAULT_FAN_DELTA_OFF_DECIC;
        fields[0]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,relay_open_temp),&default_on,sizeof(default_on)};
        fields[1]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,relay_close_temp),&default_off,sizeof(default_off)};
        fields[2]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,fan_manual_mode),&manual,sizeof(manual)};
        fields[3]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,fan_manual_state),&state,sizeof(state)};
        fields[4]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,fan_strategy),&strategy,sizeof(strategy)};
        fields[5]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,object_temperature_source),&default_object_source,sizeof(default_object_source)};
        fields[6]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,ambient_temperature_source),&default_ambient_source,sizeof(default_ambient_source)};
        fields[7]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,fan_delta_on_decic),&delta,sizeof(delta)};
        fields[8]=(ConfigurationFieldUpdate){offsetof(ControlConfigs,fan_delta_off_decic),&second_delta,sizeof(second_delta)};
        count=9;break;
    default: return ESP_ERR_INVALID_ARG;
    }
    return owner->UpdateFields(owner, "control", fields, count, persist);
}
#endif
