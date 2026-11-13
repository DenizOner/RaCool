#include "Controllers/UIController/Widgets/WidgetFan.h"
#include <stdio.h>
#include <stdbool.h>
static const LocalizationMessage sourceNames[] = {LOC_CLIMATE_INTERNAL_SOURCE, LOC_CLIMATE_EXTERNAL_SOURCE_ONE, LOC_CLIMATE_EXTERNAL_SOURCE_TWO, LOC_CLIMATE_EXTERNAL_SOURCE_THREE};
bool WidgetFan_GetValue(UIState state, SystemSettings *settings, char *value, size_t valueLen,
    char *unit, size_t unitLen, int *percent, const LocalizationCatalog *catalog) {
    const ControlConfigs *c = &settings->control;
    const char *text = NULL;
    int position = 0;
    if (state == UI_STATE_FAN_MODE) {
        text = c->fan_manual_mode ? Localization_Text(catalog,c->fan_manual_state ? LOC_CLIMATE_ALWAYS_ON : LOC_COMMON_OFF) :
            Localization_Text(catalog,c->fan_strategy == 1 ? LOC_CLIMATE_AMBIENT_SENSITIVE : LOC_CLIMATE_OBJECT_SENSITIVE);
        position = c->fan_manual_mode ? (c->fan_manual_state ? 2 : 3) : c->fan_strategy;
        snprintf(unit, unitLen, "%s", "");
        if (percent) *percent = position * 100 / 3;
    } else if (state == UI_STATE_OBJECT_SOURCE || state == UI_STATE_AMBIENT_SOURCE) {
        position = state == UI_STATE_OBJECT_SOURCE ? c->object_temperature_source : c->ambient_temperature_source;
        text = Localization_Text(catalog,position < 4 ? sourceNames[position] : LOC_COMMON_INVALID);
        snprintf(unit, unitLen, "%s", "");
        if (percent) *percent = position < 4 ? position * 100 / 3 : 0;
    } else if (state == UI_STATE_FAN_DELTA_ON || state == UI_STATE_FAN_DELTA_OFF) {
        int delta = state == UI_STATE_FAN_DELTA_ON ? c->fan_delta_on_decic : c->fan_delta_off_decic;
        snprintf(value, valueLen, "%+.1f", delta / 10.0f);
        snprintf(unit, unitLen, "°C");
        if (percent) *percent = (delta + 90) * 100 / 180;
        return true;
    } else return false;
    snprintf(value, valueLen, "%s", text);
    return true;
}
bool WidgetFan_FormatLabel(UIState state, const char *label, SystemSettings *settings, char *out, size_t length, const LocalizationCatalog *catalog) {
    if (state==UI_STATE_FAN_DELTA_OFF) {
        float factor=settings->system.temp_unit ? 1.8f : 1.0f;
        snprintf(out,length,"%s: %+.1f-%+.1f %c",label,settings->control.fan_delta_off_decic*factor/10.0f,
            settings->control.fan_delta_on_decic*factor/10.0f,settings->system.temp_unit ? 'F' : 'C');
        return true;
    }
    char value[128], unit[12];
    if (!WidgetFan_GetValue(state, settings, value, sizeof(value), unit, sizeof(unit), NULL, catalog)) return false;
    snprintf(out, length, "%s: %s %s", label, value, unit);
    return true;
}
