#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "Core/Localization/Localization.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Format menu item label based on current system settings.
 */
void FormatMenuItemLabel(UIState state, const char *label,
                         SystemSettings *settings, char *outBuf,
                         size_t maxLen, const LocalizationCatalog *catalog);

/**
 * @brief Get value and unit text representaton for submenu states.
 */
void GetSubmenuValueAndUnit(UIState state, SystemSettings *settings,
                            IMenuManager *menu, char *valBuf,
                            size_t valLen, char *unitBuf, size_t unitLen,
                            int *percent, const LocalizationCatalog *catalog);

#ifdef __cplusplus
}
#endif

#endif // UI_WIDGETS_H
