#include "Controllers/UIController/Widgets/WidgetTemp.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>


bool WidgetTemp_FormatLabel(UIState state, const char *label,
                            SystemSettings *settings,
                            char *outBuf, size_t maxLen, const LocalizationCatalog *catalog) {
    switch (state) {
    case UI_STATE_SET_LOW_TEMP: {
        char unitChar = (settings->system.temp_unit == 1) ? 'F' : 'C';
        float val     = settings->control.relay_close_temp;
        float dispVal = (settings->system.temp_unit == 1) ? (val * 1.8f + 32.0f) : val;
        float high=settings->control.relay_open_temp;
        if (settings->system.temp_unit==1) high=high*1.8f+32.0f;
        snprintf(outBuf, maxLen, "%s: %.1f-%.1f %c", label, dispVal,high,unitChar);
        return true;
    }
    case UI_STATE_SET_HIGH_TEMP: {
        char unitChar = (settings->system.temp_unit == 1) ? 'F' : 'C';
        float val     = settings->control.relay_open_temp;
        float dispVal = (settings->system.temp_unit == 1) ? (val * 1.8f + 32.0f) : val;
        snprintf(outBuf, maxLen, "%s: %.1f %c", label, dispVal, unitChar);
        return true;
    }
    case UI_STATE_SET_TEMP_UNIT:
        snprintf(outBuf, maxLen, "%s: %s", label,
                 Localization_Text(catalog,settings->system.temp_unit == 1 ? LOC_TEMPERATURE_FAHRENHEIT : LOC_TEMPERATURE_CELSIUS));
        return true;
    case UI_STATE_CALIBRATE_OBJECT:
        snprintf(outBuf, maxLen, "%s: %.1f", label, settings->calibration.ir_object_cal);
        return true;
    case UI_STATE_CALIBRATE_AMBIENT:
        snprintf(outBuf, maxLen, "%s: %.1f", label, settings->calibration.ir_ambient_cal);
        return true;
    default:
        return false;
    }
}

bool WidgetTemp_GetValue(UIState state, SystemSettings *settings,
                         char *valBuf, size_t valLen,
                         char *unitBuf, size_t unitLen,
                         int *percent, const LocalizationCatalog *catalog) {
    switch (state) {
    case UI_STATE_SET_LOW_TEMP: {
        char unitChar = (settings->system.temp_unit == 1) ? 'F' : 'C';
        float val     = settings->control.relay_close_temp;
        float dispVal = (settings->system.temp_unit == 1) ? (val * 1.8f + 32.0f) : val;
        snprintf(valBuf, valLen, "%.1f", dispVal);
        snprintf(unitBuf, unitLen, "°%c", unitChar);
        if (percent) *percent = (int)((val - FAN_SETPOINT_MIN_C) * 100.0f / (FAN_SETPOINT_MAX_C - FAN_SETPOINT_MIN_C));
        return true;
    }
    case UI_STATE_SET_HIGH_TEMP: {
        char unitChar = (settings->system.temp_unit == 1) ? 'F' : 'C';
        float val     = settings->control.relay_open_temp;
        float dispVal = (settings->system.temp_unit == 1) ? (val * 1.8f + 32.0f) : val;
        snprintf(valBuf, valLen, "%.1f", dispVal);
        snprintf(unitBuf, unitLen, "°%c", unitChar);
        if (percent) *percent = (int)((val - FAN_SETPOINT_MIN_C) * 100.0f / (FAN_SETPOINT_MAX_C - FAN_SETPOINT_MIN_C));
        return true;
    }
    case UI_STATE_SET_TEMP_UNIT:
        snprintf(valBuf, valLen, "%s", Localization_Text(catalog,settings->system.temp_unit == 1 ? LOC_TEMPERATURE_FAHRENHEIT : LOC_TEMPERATURE_CELSIUS));
        unitBuf[0]='\0';
        if (percent) *percent = settings->system.temp_unit ? 100 : 0;
        return true;
    case UI_STATE_CALIBRATE_OBJECT:
        snprintf(valBuf, valLen, "%.2f", settings->calibration.ir_object_cal);
        snprintf(unitBuf,unitLen,"°C");
        if (percent)
            *percent = (int)((settings->calibration.ir_object_cal - CALIBRATION_MIN) * 100.0f /
                             (CALIBRATION_MAX - CALIBRATION_MIN));
        return true;
    case UI_STATE_CALIBRATE_AMBIENT:
        snprintf(valBuf, valLen, "%.2f", settings->calibration.ir_ambient_cal);
        snprintf(unitBuf,unitLen,"°C");
        if (percent)
            *percent = (int)((settings->calibration.ir_ambient_cal - CALIBRATION_MIN) * 100.0f /
                             (CALIBRATION_MAX - CALIBRATION_MIN));
        return true;
    default:
        return false;
    }
}
