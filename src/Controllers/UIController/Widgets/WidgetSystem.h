#ifndef WIDGET_SYSTEM_H
#define WIDGET_SYSTEM_H

#include "Core/Localization/Localization.h"
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <stdbool.h>
#include <stddef.h>

bool WidgetSystem_FormatLabel(UIState state, const char *label,
                              SystemSettings *settings,
                              char *outBuf, size_t maxLen, const LocalizationCatalog *catalog);

bool WidgetSystem_GetValue(UIState state, SystemSettings *settings,
                           char *valBuf, size_t valLen,
                           char *unitBuf, size_t unitLen,
                           int *percent, const LocalizationCatalog *catalog);

#endif /* WIDGET_SYSTEM_H */
