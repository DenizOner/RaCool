#ifndef I_MENU_MANAGER_H
#define I_MENU_MANAGER_H

#include "Core/IService.h"
#include "Controllers/MenuManager/MenuManagerConfig.h"
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include "Controllers/MenuManager/MenuPluginManager.h" // For LegacyMenuPluginAction
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// IMenuManager forward declaration
typedef struct IMenuManager IMenuManager;

struct IMenuManager {
    IService base;
    
    int32_t (*InitializeWithConfig)(IMenuManager *self, const MenuManagerConfig *config, struct IDIRegistry *registry);

    /* Copies one supported event into the worker queue; success means admission,
     * not completed UI/relay/power actuation. Legacy void inputs remain best effort. */
    MenuManagerError (*PostEvent)(IMenuManager *self, const MenuEvent *event);

    void (*ProcessEncoder)(IMenuManager *self, int8_t direction);

    void (*ProcessButton)(IMenuManager *self);

    void (*UpdateActivity)(IMenuManager *self);

    void (*ProcessLongPress)(IMenuManager *self);

    UIState (*GetCurrentState)(const IMenuManager *self);

    bool (*IsEditing)(const IMenuManager *self);

    void (*SetEditing)(IMenuManager *self, bool editing);

    UIState (*GetActiveCategory)(const IMenuManager *self);

    uint32_t (*GetLastActivityTime)(const IMenuManager *self);

    void (*SetState)(IMenuManager *self, UIState state);

    /* Copy a stable label into caller-owned storage; failure leaves it unchanged. */
    bool (*CopyLabel)(const IMenuManager *self, UIState state, char *output, size_t capacity);

    MenuManagerError (*RegisterPluginEx)(IMenuManager *self, const char *label, UIState state, MenuPluginHandlerEx handler);

    MenuManagerError (*RegisterPlugin)(IMenuManager *self, const char *label, UIState state, LegacyMenuPluginAction onAction);

    MenuManagerError (*UnregisterPlugin)(IMenuManager *self, UIState state);

    MenuManagerError (*RegisterPluginBatch)(IMenuManager *self, const MenuPluginRegistration *entries, size_t count);
    MenuManagerError (*UnregisterPluginBatch)(IMenuManager *self, const MenuPluginRegistration *entries, size_t count);

    void (*GoBack)(IMenuManager *self);

    void (*ConfirmAction)(IMenuManager *self, bool accept);

    bool (*GetConfirmSelection)(const IMenuManager *self);

    /* Full uptime milliseconds for deadlines across more than one 32-bit wrap.
     * The legacy GetLastActivityTime remains the low 32-bit compatibility view. */
    uint64_t (*GetLastActivityTimeMs)(const IMenuManager *self);
};

IMenuManager* GetMenuManager(void);

IMenuManager* MenuManager_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif // I_MENU_MANAGER_H
