#ifndef MENU_MANAGER_EVENTS_H
#define MENU_MANAGER_EVENTS_H

/**
 * @file MenuManagerEvents.h
 * @brief Menü modülü tarafından EventBus üzerinden yayınlanan olaylar
 */

// EventBus Event İsimleri
#define EVENT_MENU_STATE_CHANGED    "menu.state.changed"
#define EVENT_MENU_EDITING_STARTED  "menu.editing.started"
#define EVENT_MENU_EDITING_FINISHED "menu.editing.finished"
#define EVENT_MENU_NAVIGATE_TO      "menu.navigate"
#define EVENT_MENU_CONFIRM_ACCEPTED "menu.confirm.accepted"
#define EVENT_MENU_CONFIRM_REJECTED "menu.confirm.rejected"
#define EVENT_MENU_PLUGIN_REGISTERED "menu.plugin.registered"
#define EVENT_MENU_ACTIVITY         "menu.activity"

#endif // MENU_MANAGER_EVENTS_H
