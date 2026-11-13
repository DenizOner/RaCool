#include "Controllers/MenuManager/MenuPluginManager.h"
#include "Controllers/MenuManager/MenuManagerConfig.h"
#include <freertos/FreeRTOS.h>
#include <string.h>

typedef struct {
    char label[MENU_LABEL_CAPACITY];
    MenuPluginHandlerEx handlerEx;
    LegacyMenuPluginAction handlerLegacy;
    unsigned active;
    bool enabled;
    bool registered;
} MenuPluginEntry;

static MenuPluginEntry plugins[UI_STATE_COUNT];
static portMUX_TYPE pluginLock = portMUX_INITIALIZER_UNLOCKED;

/* Startup-only reset before publishing the service. */
void MenuPluginManager_Init(void) {
    portENTER_CRITICAL(&pluginLock);
    memset(plugins, 0, sizeof(plugins));
    portEXIT_CRITICAL(&pluginLock);
}

static MenuManagerError ValidateBatch(const MenuPluginRegistration *entries, size_t count) {
    if (!entries || !count || count > UI_STATE_COUNT) return MENU_ERR_INVALID_STATE;
    for (size_t i = 0; i < count; ++i) {
        const MenuPluginRegistration *entry = &entries[i];
        if ((unsigned)entry->state >= UI_STATE_COUNT || ((entry->modern == NULL) == (entry->legacy == NULL)))
            return MENU_ERR_INVALID_STATE;
        if (entry->label && strnlen(entry->label, MENU_LABEL_CAPACITY) >= MENU_LABEL_CAPACITY)
            return MENU_ERR_INVALID_STATE;
        for (size_t j = 0; j < i; ++j)
            if (entries[j].state == entry->state) return MENU_ERR_INVALID_STATE;
    }
    return MENU_OK;
}

MenuManagerError MenuPluginManager_RegisterBatch(const MenuPluginRegistration *entries, size_t count) {
    MenuManagerError result = ValidateBatch(entries, count);
    if (result != MENU_OK) return result;
    portENTER_CRITICAL(&pluginLock);
    for (size_t i = 0; i < count; ++i) {
        if (plugins[entries[i].state].active) { result = MENU_ERR_BUSY; break; }
    }
    if (result == MENU_OK) {
        for (size_t i = 0; i < count; ++i) {
            const MenuPluginRegistration *source = &entries[i];
            MenuPluginEntry *entry = &plugins[source->state];
            memset(entry, 0, sizeof(*entry));
            if (source->label) memcpy(entry->label, source->label, strlen(source->label));
            entry->handlerEx = source->modern;
            entry->handlerLegacy = source->legacy;
            entry->enabled = entry->registered = true;
        }
    }
    portEXIT_CRITICAL(&pluginLock);
    return result;
}

MenuManagerError MenuPluginManager_UnregisterBatch(const MenuPluginRegistration *entries, size_t count) {
    MenuManagerError result = ValidateBatch(entries, count);
    if (result != MENU_OK) return result;
    portENTER_CRITICAL(&pluginLock);
    for (size_t i = 0; i < count; ++i) {
        const MenuPluginEntry *entry = &plugins[entries[i].state];
        if (entry->active) { result = MENU_ERR_BUSY; break; }
        if (!entry->registered || entry->handlerEx != entries[i].modern || entry->handlerLegacy != entries[i].legacy) {
            result = MENU_ERR_INVALID_STATE;
            break;
        }
    }
    if (result == MENU_OK)
        for (size_t i = 0; i < count; ++i) memset(&plugins[entries[i].state], 0, sizeof(MenuPluginEntry));
    portEXIT_CRITICAL(&pluginLock);
    return result;
}

static MenuManagerError RegisterEntry(UIState state, const char *label,
                                      MenuPluginHandlerEx modern, LegacyMenuPluginAction legacy) {
    const MenuPluginRegistration entry = {state, label, modern, legacy};
    return MenuPluginManager_RegisterBatch(&entry, 1);
}

MenuManagerError MenuPluginManager_Register(UIState state, const char *label, MenuPluginHandlerEx handler) {
    return RegisterEntry(state, label, handler, NULL);
}
MenuManagerError MenuPluginManager_RegisterLegacy(UIState state, const char *label, LegacyMenuPluginAction handler) {
    return RegisterEntry(state, label, NULL, handler);
}

MenuManagerError MenuPluginManager_Unregister(UIState state) {
    if ((unsigned)state >= UI_STATE_COUNT) return MENU_ERR_INVALID_STATE;
    portENTER_CRITICAL(&pluginLock);
    MenuPluginEntry *entry = &plugins[state];
    MenuManagerError result = !entry->registered ? MENU_ERR_NOT_FOUND :
                              entry->active ? MENU_ERR_BUSY : MENU_OK;
    if (result == MENU_OK) memset(entry, 0, sizeof(*entry));
    portEXIT_CRITICAL(&pluginLock);
    return result;
}

MenuManagerError MenuPluginManager_SetEnabled(UIState state, bool enabled) {
    if ((unsigned)state >= UI_STATE_COUNT) return MENU_ERR_INVALID_STATE;
    portENTER_CRITICAL(&pluginLock);
    MenuPluginEntry *entry = &plugins[state];
    MenuManagerError result = !entry->registered ? MENU_ERR_NOT_FOUND :
                              entry->active ? MENU_ERR_BUSY : MENU_OK;
    if (result == MENU_OK) entry->enabled = enabled;
    portEXIT_CRITICAL(&pluginLock);
    return result;
}

bool MenuPluginManager_DispatchEvent(MenuPluginEvent *event) {
    if (!event) return false;
    event->handled = false;
    if ((unsigned)event->state >= UI_STATE_COUNT ||
        (unsigned)event->action > MENU_PLUGIN_ACTION_EXIT) return false;
    UIState state = event->state;
    portENTER_CRITICAL(&pluginLock);
    MenuPluginEntry snapshot = plugins[state];
    /* One callback per entry: reject recursive or concurrent dispatch. */
    bool accepted = snapshot.registered && snapshot.enabled && !snapshot.active;
    if (accepted) plugins[state].active = 1;
    portEXIT_CRITICAL(&pluginLock);
    if (!accepted) return false;

    /* Callbacks may block or call registry APIs; never hold the critical section. */
    if (snapshot.handlerLegacy) {
        if (event->action == MENU_PLUGIN_ACTION_ENCODER || event->action == MENU_PLUGIN_ACTION_BUTTON) {
            snapshot.handlerLegacy(state,
                event->action == MENU_PLUGIN_ACTION_ENCODER ? event->direction : 0,
                event->action == MENU_PLUGIN_ACTION_BUTTON);
            event->handled = true;
        }
    } else if (snapshot.handlerEx) {
        snapshot.handlerEx(event);
        /* The modern handler takes a const event; delivery is the handled contract. */
        event->handled = true;
    }
    portENTER_CRITICAL(&pluginLock);
    plugins[state].active = 0;
    portEXIT_CRITICAL(&pluginLock);
    return event->handled;
}

bool MenuPluginManager_HasPlugin(UIState state) {
    if ((unsigned)state >= UI_STATE_COUNT) return false;
    portENTER_CRITICAL(&pluginLock);
    bool found = plugins[state].registered && plugins[state].enabled;
    portEXIT_CRITICAL(&pluginLock);
    return found;
}

bool MenuPluginManager_CopyLabel(UIState state, char *output, size_t capacity) {
    if ((unsigned)state >= UI_STATE_COUNT || !output || !capacity) return false;
    char label[MENU_LABEL_CAPACITY];
    portENTER_CRITICAL(&pluginLock);
    bool found = plugins[state].registered;
    if (found) memcpy(label, plugins[state].label, sizeof(label));
    portEXIT_CRITICAL(&pluginLock);
    if (!found) return false;
    size_t length = strlen(label);
    if (capacity <= length) return false;
    memcpy(output, label, length + 1);
    return true;
}
