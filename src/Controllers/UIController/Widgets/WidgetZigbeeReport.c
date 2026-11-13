#include "Controllers/UIController/Widgets/WidgetZigbeeReport.h"
#include <stdio.h>
#include <stdbool.h>

/* Maximum Zigbee reporting interval for progress bar scaling. */
#define ZB_REP_MAX_SEC ZB_REPORT_MAX_S

bool WidgetZigbeeReport_FormatLabel(UIState state, const char *label,
                                    SystemSettings *settings,
                                    char *outBuf, size_t maxLen, const LocalizationCatalog *catalog) {
    if (!settings || !label || !outBuf || !maxLen) return false;
    switch (state) {
    case UI_STATE_ZB_REP_FAN:
        if (settings->zigbee_reporting.fan_s == 0) snprintf(outBuf, maxLen, "%s: %s", label, Localization_Text(catalog,LOC_COMMON_OFF));
        else snprintf(outBuf, maxLen, "%s: %u s", label, settings->zigbee_reporting.fan_s);
        return true;
    case UI_STATE_ZB_REP_AMBIENT:
        if (settings->zigbee_reporting.ambient_s == 0) snprintf(outBuf, maxLen, "%s: %s", label, Localization_Text(catalog,LOC_COMMON_OFF));
        else snprintf(outBuf, maxLen, "%s: %u s", label, settings->zigbee_reporting.ambient_s);
        return true;
    case UI_STATE_ZB_REP_CPU:
        if (settings->zigbee_reporting.cpu_s == 0) snprintf(outBuf, maxLen, "%s: %s", label, Localization_Text(catalog,LOC_COMMON_OFF));
        else snprintf(outBuf, maxLen, "%s: %u s", label, settings->zigbee_reporting.cpu_s);
        return true;
    case UI_STATE_ZB_REP_HEALTH:
        if (settings->zigbee_reporting.health_s == 0) snprintf(outBuf, maxLen, "%s: %s", label, Localization_Text(catalog,LOC_COMMON_OFF));
        else snprintf(outBuf, maxLen, "%s: %u s", label, settings->zigbee_reporting.health_s);
        return true;
    case UI_STATE_ZB_REP_MEMORY:
        if (settings->zigbee_reporting.memory_s == 0) snprintf(outBuf, maxLen, "%s: %s", label, Localization_Text(catalog,LOC_COMMON_OFF));
        else snprintf(outBuf, maxLen, "%s: %u s", label, settings->zigbee_reporting.memory_s);
        return true;
    case UI_STATE_ZB_REP_UPTIME:
        if (settings->zigbee_reporting.uptime_s == 0) snprintf(outBuf, maxLen, "%s: %s", label, Localization_Text(catalog,LOC_COMMON_OFF));
        else snprintf(outBuf, maxLen, "%s: %u s", label, settings->zigbee_reporting.uptime_s);
        return true;
    default:
        return false;
    }
}

bool WidgetZigbeeReport_GetValue(UIState state, SystemSettings *settings,
                                 char *valBuf, size_t valLen,
                                 char *unitBuf, size_t unitLen,
                                 int *percent, const LocalizationCatalog *catalog) {
    if (!settings || !valBuf || !valLen || !unitBuf || !unitLen) return false;
    uint16_t val = 0;
    switch (state) {
    case UI_STATE_ZB_REP_FAN:     val = settings->zigbee_reporting.fan_s;     break;
    case UI_STATE_ZB_REP_AMBIENT: val = settings->zigbee_reporting.ambient_s; break;
    case UI_STATE_ZB_REP_CPU:     val = settings->zigbee_reporting.cpu_s;     break;
    case UI_STATE_ZB_REP_HEALTH:  val = settings->zigbee_reporting.health_s;  break;
    case UI_STATE_ZB_REP_MEMORY:  val = settings->zigbee_reporting.memory_s;  break;
    case UI_STATE_ZB_REP_UPTIME:  val = settings->zigbee_reporting.uptime_s;  break;
    default: return false;
    }
    if (val == 0) snprintf(valBuf,valLen,"%s",Localization_Text(catalog,LOC_COMMON_OFF));
    else snprintf(valBuf, valLen, "%u", val);
    snprintf(unitBuf, unitLen, "%s", val == 0 ? "" : "s");
    if (percent) *percent = val >= ZB_REP_MAX_SEC ? 100 : (int)(val * 100 / ZB_REP_MAX_SEC);
    return true;
}
