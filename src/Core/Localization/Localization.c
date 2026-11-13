#include "Core/Localization/Localization.h"
#include <stdatomic.h>
#include <string.h>

static _Atomic(const LocalizationCatalog *) active_catalog = NULL;

size_t Localization_Count(void) {
    return localization_catalog_count;
}

const LocalizationCatalog *Localization_At(size_t index) {
    return index < localization_catalog_count ? &localization_catalogs[index] : NULL;
}

const LocalizationCatalog *Localization_Find(const char *code) {
    if (!code) return NULL;
    for (size_t index = 0; index < localization_catalog_count; ++index)
        if (strcmp(localization_catalogs[index].code, code) == 0)
            return &localization_catalogs[index];
    return NULL;
}

const LocalizationCatalog *Localization_Snapshot(void) {
    const LocalizationCatalog *catalog = atomic_load_explicit(&active_catalog, memory_order_acquire);
    return catalog ? catalog : Localization_Find("tr");
}

const char *Localization_Text(const LocalizationCatalog *catalog, LocalizationMessage message) {
    if (!catalog || (unsigned)message >= LOC_MESSAGE_COUNT) return "";
    return catalog->messages[message];
}

bool Localization_Apply(const char *code) {
    const LocalizationCatalog *catalog = Localization_Find(code);
    if (!catalog) return false;
    atomic_store_explicit(&active_catalog, catalog, memory_order_release);
    return true;
}

const char *Localization_TextKey(const LocalizationCatalog *catalog, const char *key) {
    if (!catalog || !key) return NULL;
    size_t first = 0, last = LOC_MESSAGE_COUNT;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        int order = strcmp(key, localization_message_keys[middle]);
        if (order == 0) return catalog->messages[middle];
        if (order < 0) last = middle;
        else first = middle + 1;
    }
    return NULL;
}
