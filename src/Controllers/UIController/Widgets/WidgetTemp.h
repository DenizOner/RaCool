#ifndef WIDGET_TEMP_H
#define WIDGET_TEMP_H

#include "Core/Localization/Localization.h"
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <stdbool.h>
#include <stddef.h>

bool WidgetTemp_FormatLabel(UIState state, const char *label,
                            SystemSettings *settings,
                            char *outBuf, size_t maxLen, const LocalizationCatalog *catalog);

bool WidgetTemp_GetValue(UIState state, SystemSettings *settings,
                         char *valBuf, size_t valLen,
                         char *unitBuf, size_t unitLen,
                         int *percent, const LocalizationCatalog *catalog);

#endif /* WIDGET_TEMP_H */
