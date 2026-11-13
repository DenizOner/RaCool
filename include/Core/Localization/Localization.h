#ifndef RACOOL_LOCALIZATION_H
#define RACOOL_LOCALIZATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "LocalizationCatalog.generated.h"

/** Catalogs are immutable flash storage and remain valid for firmware lifetime. */
size_t Localization_Count(void);
const LocalizationCatalog *Localization_At(size_t index);
const LocalizationCatalog *Localization_Find(const char *code);
/** Capture once per frame/response to avoid mixed-language output. */
const LocalizationCatalog *Localization_Snapshot(void);
const char *Localization_Text(const LocalizationCatalog *catalog, LocalizationMessage message);
/** Resolve an extensible semantic key; NULL means missing key/invalid input. */
const char *Localization_TextKey(const LocalizationCatalog *catalog, const char *key);
/** Apply only after the configuration owner has checked persistence success. */
bool Localization_Apply(const char *code);

/* The unreachable reference call retains compiler checking of argument types.
 * Catalog generation separately enforces the same conversion/length contract. */
#define LOC_PRINTF(catalog, message, reference, ...) do { \
    if (0) printf(reference, ##__VA_ARGS__); \
    printf(Localization_Text((catalog), (message)), ##__VA_ARGS__); \
} while (0)

#endif
