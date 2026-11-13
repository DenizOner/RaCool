#ifndef WIDGET_RGB_H
#define WIDGET_RGB_H

#include "Core/Localization/Localization.h"
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <stdbool.h>
#include <stddef.h>

bool WidgetRGB_FormatLabel(UIState state, const char *label,
                           SystemSettings *settings,
                           char *outBuf, size_t maxLen, const LocalizationCatalog *catalog);

bool WidgetRGB_GetValue(UIState state, SystemSettings *settings,
                        char *valBuf, size_t valLen,
                        char *unitBuf, size_t unitLen,
                        int *percent, const LocalizationCatalog *catalog);

#endif /* WIDGET_RGB_H */
