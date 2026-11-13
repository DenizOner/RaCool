#ifndef RACOOL_UI_TRANSLATION_H
#define RACOOL_UI_TRANSLATION_H

#include "Core/Localization/Localization.h"
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"

/* Explicit catalog arguments keep a whole frame in one immutable locale. */
const char *UITranslation_MenuLabel(UIState state, const char *fallback,
                                   const LocalizationCatalog *catalog);
const char *UITranslation_Animation(uint8_t id, bool builtin,
                                   const LocalizationCatalog *catalog);
const char *UITranslation_Color(PresetColorIndex color,
                               const LocalizationCatalog *catalog);

#endif
