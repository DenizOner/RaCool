#ifndef MENU_PLUGIN_MANAGER_H
#define MENU_PLUGIN_MANAGER_H
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*LegacyMenuPluginAction)(UIState state, int8_t direction, bool isButtonPressed);
typedef struct {
    UIState state;
    const char *label;
    MenuPluginHandlerEx modern;
    LegacyMenuPluginAction legacy;
} MenuPluginRegistration;
/* All-or-nothing group registration/removal. Descriptor storage must remain
 * stable during the call. Removal requires matching callback ownership. */
MenuManagerError MenuPluginManager_RegisterBatch(const MenuPluginRegistration *entries, size_t count);
MenuManagerError MenuPluginManager_UnregisterBatch(const MenuPluginRegistration *entries, size_t count);
/* Startup only, before any concurrent consumer. */
void MenuPluginManager_Init(void);
/* Caller-owned label is copied. Overlong labels are rejected without changing the entry.
 * Replacement/removal/disable returns BUSY while a callback is in flight. Consumers
 * must check that result before releasing callback-owned resources. */
MenuManagerError MenuPluginManager_Register(UIState state, const char *label, MenuPluginHandlerEx handler);
MenuManagerError MenuPluginManager_RegisterLegacy(UIState state, const char *label, LegacyMenuPluginAction handler);
MenuManagerError MenuPluginManager_Unregister(UIState state);
MenuManagerError MenuPluginManager_SetEnabled(UIState state, bool enabled);
/* Callbacks run outside the registry lock. Recursive/concurrent dispatch to the
 * same entry is rejected. Callback code must return normally. */
bool MenuPluginManager_DispatchEvent(MenuPluginEvent *event);
bool MenuPluginManager_HasPlugin(UIState state);
/* Leaves output unchanged on missing state, invalid input, or insufficient capacity. */
bool MenuPluginManager_CopyLabel(UIState state, char *output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
