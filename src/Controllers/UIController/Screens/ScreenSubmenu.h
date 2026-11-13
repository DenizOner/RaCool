#ifndef SCREEN_SUBMENU_H
#define SCREEN_SUBMENU_H

#include "Core/Localization/Localization.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <stdint.h>

bool ScreenSubmenu_Create(void);

void ScreenSubmenu_Update(SystemSettings *settings, const LocalizationCatalog *catalog);

#endif /* SCREEN_SUBMENU_H */
