#ifndef RACOOL_CONFIGURATION_CONSOLE_VIEW_H
#define RACOOL_CONFIGURATION_CONSOLE_VIEW_H
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/ConfigurationObservableFields.h"
#include "Core/Localization/Localization.h"

/* Both console entry points expose one complete immutable owner snapshot. */
static inline void ConfigurationConsole_PrintSnapshot(const SystemSettings *snapshot,const LocalizationCatalog *catalog) {
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_SHOW_TITLE,"--- Current Configuration ---\n");
    unsigned fields=0;
#define CONFIG_SHOW_NUMBER(section,field) \
    printf("RACOOL_CLI v=1 type=config_field key=" #section "." #field " value=%.9g\n",(double)snapshot->section.field); \
    ++fields;
    CONFIGURATION_OBSERVABLE_NUMBERS(CONFIG_SHOW_NUMBER)
#undef CONFIG_SHOW_NUMBER
    printf("RACOOL_CLI v=1 type=config_field key=localization.locale value=%s\n",snapshot->localization.locale);
    printf("RACOOL_CLI v=1 type=config_field key=restart_required value=%u\n",snapshot->restart_required ? 1U : 0U);
    printf("RACOOL_CLI v=1 type=config_snapshot fields=%u\n",fields+2);
}
#endif
