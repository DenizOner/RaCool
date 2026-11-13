#include "Controllers/UIController/UIWidgets.h"
#include "Controllers/UIController/Widgets/WidgetTemp.h"
#include "Controllers/UIController/Widgets/WidgetFan.h"
#include "Controllers/UIController/Widgets/WidgetLED.h"
#include "Controllers/UIController/Widgets/WidgetRGB.h"
#include "Controllers/UIController/Widgets/WidgetSystem.h"
#include "Controllers/UIController/Widgets/WidgetZigbeeReport.h"
#include "Controllers/MenuManager/MenuStateMachine.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Format a menu item label with its current value.
 *
 * Dispatches to the appropriate domain Widget based on UIState.
 * Each Widget module owns exactly one domain (SRP).
 */
void FormatMenuItemLabel(UIState state, const char *label,
                         SystemSettings *settings, char *outBuf,
                         size_t maxLen, const LocalizationCatalog *catalog) {
    if (!settings || !label || !outBuf || maxLen == 0) return;

    // Default: show label as-is
    snprintf(outBuf, maxLen, "%s", label);

    if (WidgetTemp_FormatLabel(state, label, settings, outBuf, maxLen, catalog))         return;
    if (WidgetFan_FormatLabel(state, label, settings, outBuf, maxLen, catalog))          return;
    if (WidgetLED_FormatLabel(state, label, settings, outBuf, maxLen, catalog))          return;
    if (WidgetRGB_FormatLabel(state, label, settings, outBuf, maxLen, catalog))          return;
    if (WidgetSystem_FormatLabel(state, label, settings, outBuf, maxLen, catalog))       return;
    if (WidgetZigbeeReport_FormatLabel(state, label, settings, outBuf, maxLen, catalog)) return;
}

/**
 * @brief Get submenu value, unit string, and progress percentage.
 *
 * Dispatches to the appropriate domain Widget based on UIState.
 */
void GetSubmenuValueAndUnit(UIState state, SystemSettings *settings,
                            IMenuManager *menu, char *valBuf,
                            size_t valLen, char *unitBuf, size_t unitLen,
                            int *percent, const LocalizationCatalog *catalog) {
    if (!settings || !valBuf || !unitBuf || valLen == 0 || unitLen == 0) return;

    valBuf[0]  = '\0';
    unitBuf[0] = '\0';
    if (percent) *percent = 0;

    if (WidgetTemp_GetValue(state, settings, valBuf, valLen, unitBuf, unitLen, percent, catalog))         return;
    if (WidgetFan_GetValue(state, settings, valBuf, valLen, unitBuf, unitLen, percent, catalog))          return;
    if (WidgetLED_GetValue(state, settings, valBuf, valLen, unitBuf, unitLen, percent, catalog))          return;
    if (WidgetRGB_GetValue(state, settings, valBuf, valLen, unitBuf, unitLen, percent, catalog))          return;
    if (WidgetSystem_GetValue(state, settings, valBuf, valLen, unitBuf, unitLen, percent, catalog))       return;
    if (WidgetZigbeeReport_GetValue(state, settings, valBuf, valLen, unitBuf, unitLen, percent, catalog)) return;

    // Fallback: confirmation dialogs and unknown states
    if (menu && MenuStateMachine_RequiresConfirmation(state)) {
        bool confirm = menu->GetConfirmSelection(menu);
        snprintf(valBuf, valLen, "%s", Localization_Text(catalog,confirm ? LOC_COMMON_YES : LOC_COMMON_NO));
        if (percent) *percent = confirm ? 100 : 0;
    } else {
        snprintf(valBuf, valLen, "---");
    }
}
