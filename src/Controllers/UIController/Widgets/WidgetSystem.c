#include "Controllers/UIController/Widgets/WidgetSystem.h"
#include <stdio.h>
#include <stdbool.h>
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"

static int RangePercent(uint32_t value, uint32_t minimum, uint32_t maximum) {
    if (value <= minimum) return 0;
    if (value >= maximum) return 100;
    return (int)(((uint64_t)value - minimum) * 100 / (maximum - minimum));
}

static const char *PowerProfileLabel(uint8_t profile,const LocalizationCatalog *catalog) {
    static const LocalizationMessage labels[] = {LOC_POWER_PERFORMANCE,LOC_POWER_BALANCED,LOC_POWER_ECONOMY};
    return Localization_Text(catalog,profile < POWER_PROFILE_COUNT ? labels[profile] : LOC_COMMON_UNKNOWN);
}
static const char *TxProfileLabel(uint8_t profile,const LocalizationCatalog *catalog) {
    static const LocalizationMessage labels[] = {LOC_RADIO_ECO,LOC_RADIO_NORMAL,LOC_RADIO_BOOST};
    return Localization_Text(catalog,profile < ZIGBEE_TX_PROFILE_COUNT ? labels[profile] : LOC_COMMON_UNKNOWN);
}

bool WidgetSystem_FormatLabel(UIState state, const char *label,
                              SystemSettings *settings,
                              char *outBuf, size_t maxLen, const LocalizationCatalog *catalog) {
    switch (state) {
    case UI_STATE_SET_DISPLAY_MODE:
        snprintf(outBuf,maxLen,"%s: %s",label,Localization_Text(catalog,
            settings->system.display_mode==2 ? LOC_MENU_DISPLAY_ALWAYS_ON : LOC_COMMON_AUTOMATIC));
        return true;
    case UI_STATE_SET_MENU_AUTO_EXIT:
        snprintf(outBuf,maxLen,"%s: %s",label,Localization_Text(catalog,
            settings->system.menu_auto_exit_enabled ? LOC_COMMON_ON : LOC_COMMON_OFF));
        return true;
    case UI_STATE_SET_LANGUAGE:
        snprintf(outBuf,maxLen,"%s: %s",label,catalog->name);
        return true;
    case UI_STATE_SET_ZIGBEE_TX_PROFILE:
        snprintf(outBuf, maxLen, "%s: %s", label, TxProfileLabel(settings->system.zigbee_tx_profile,catalog));
        return true;
    case UI_STATE_SET_POWER_PROFILE:
        snprintf(outBuf, maxLen, "%s: %s", label, PowerProfileLabel(settings->system.power_profile,catalog));
        return true;
    case UI_STATE_SET_TIMEOUT:
        snprintf(outBuf, maxLen, "%s: %lu s", label,
                 (unsigned long)settings->system.oled_timeout_sec);
        return true;
    case UI_STATE_SET_TELEMETRY:
        if (!settings->system.telemetry_interval_ms)
            snprintf(outBuf, maxLen, "%s: %s", label, Localization_Text(catalog,LOC_COMMON_OFF));
        else snprintf(outBuf, maxLen, "%s: %lu s", label,
                      (unsigned long)(settings->system.telemetry_interval_ms / 1000));
        return true;
    default:
        return false;
    }
}

bool WidgetSystem_GetValue(UIState state, SystemSettings *settings,
                           char *valBuf, size_t valLen,
                           char *unitBuf, size_t unitLen,
                           int *percent, const LocalizationCatalog *catalog) {
    switch (state) {
    case UI_STATE_SET_DISPLAY_MODE:
        snprintf(valBuf,valLen,"%s",Localization_Text(catalog,
            settings->system.display_mode==2 ? LOC_MENU_DISPLAY_ALWAYS_ON : LOC_COMMON_AUTOMATIC));
        unitBuf[0]='\0';
        if(percent) *percent=settings->system.display_mode==2 ? 100 : 0;
        return true;
    case UI_STATE_SYSTEM_STATUS: {
        IDiagnosticsManager *diagnostics=(IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
        DiagnosticsTestResult result={0};
        if (diagnostics && diagnostics->GetLastTestResult) result=diagnostics->GetLastTestResult(diagnostics);
        if (result.completed && result.services_checked && result.health_score>=0 && result.health_score<=100) {
            snprintf(valBuf,valLen,"%ld%%",(long)result.health_score);
        } else snprintf(valBuf,valLen,"%s",Localization_Text(catalog,LOC_COMMON_UNKNOWN));
        unitBuf[0]='\0';
        if (percent) *percent=0;
        return true;
    }
    case UI_STATE_SET_LANGUAGE: {
        const LocalizationCatalog *selected=Localization_Find(settings->localization.locale);
        if(selected)catalog=selected;
        snprintf(valBuf,valLen,"%s",catalog->name);
        unitBuf[0]='\0';
        size_t count=Localization_Count(),index=0;
        while(index<count && Localization_At(index)!=catalog) ++index;
        if(percent) *percent=count>1 && index<count ? (int)(index*100/(count-1)) : 0;
        return true;
    }
    case UI_STATE_SET_MENU_AUTO_EXIT:
        snprintf(valBuf,valLen,"%s",Localization_Text(catalog,settings->system.menu_auto_exit_enabled ? LOC_COMMON_ON : LOC_COMMON_OFF));
        snprintf(unitBuf,unitLen,"%s","");
        if(percent) *percent=settings->system.menu_auto_exit_enabled ? 100 : 0;
        return true;
    case UI_STATE_SET_ZIGBEE_TX_PROFILE:
        snprintf(valBuf, valLen, "%s", TxProfileLabel(settings->system.zigbee_tx_profile,catalog));
        snprintf(unitBuf, unitLen, "%s", "");
        if (percent) *percent = settings->system.zigbee_tx_profile < ZIGBEE_TX_PROFILE_COUNT
            ? settings->system.zigbee_tx_profile * 50 : 0;
        return true;
    case UI_STATE_SET_POWER_PROFILE:
        snprintf(valBuf, valLen, "%s", PowerProfileLabel(settings->system.power_profile,catalog));
        snprintf(unitBuf, unitLen, "%s", "");
        if (percent) *percent = settings->system.power_profile < POWER_PROFILE_COUNT
            ? settings->system.power_profile * 50 : 0;
        return true;
    case UI_STATE_SET_TIMEOUT:
        snprintf(valBuf, valLen, "%lu", (unsigned long)settings->system.oled_timeout_sec);
        snprintf(unitBuf, unitLen, "s");
        if (percent) *percent = RangePercent(settings->system.oled_timeout_sec, OLED_SLEEP_ZB_MIN_SEC, OLED_SLEEP_ZB_MAX_SEC);
        return true;
    case UI_STATE_SET_TELEMETRY:
        snprintf(valBuf, valLen, "%lu",
                 (unsigned long)(settings->system.telemetry_interval_ms / 1000));
        if (!settings->system.telemetry_interval_ms) snprintf(valBuf, valLen, "%s",Localization_Text(catalog,LOC_COMMON_OFF));
        snprintf(unitBuf, unitLen, "%s", settings->system.telemetry_interval_ms ? "s" : "");
        if (percent)
            *percent = RangePercent(settings->system.telemetry_interval_ms, TELEMETRY_MIN_MS, TELEMETRY_MAX_MS);
        return true;
    default:
        return false;
    }
}
