#ifndef SCREEN_MENU_H
#define SCREEN_MENU_H

#include "Core/Localization/Localization.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <stdint.h>
#include <stdbool.h>

bool ScreenMenu_Create(void);

bool ScreenMenu_Update(SystemSettings *settings, float cachedObjectTemp, bool zigbeeConnected, const LocalizationCatalog *catalog);

#endif /* SCREEN_MENU_H */
