#include "Controllers/MenuManager/MenuRangeEditor.h"
#include "Controllers/MenuManager/MenuEditSession.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/MenuManager/MenuManagerConfig.h"
#include "Controllers/MenuManager/MenuManagerEvents.h"
#include "Controllers/MenuManager/MenuNavigator.h"
#include "Controllers/MenuManager/MenuStateMachine.h"
#include "Controllers/MenuManager/MenuPluginManager.h"
#include "Hardware/RGBLED/RGBZoneSelection.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Hardware/RotaryEncoder/IRotaryEncoder.h"
#include "Hardware/RotaryEncoder/RotaryEncoderConfig.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <string.h>

#ifndef RE_ENCODER_DEDUP_WINDOW_MS
#define RE_ENCODER_DEDUP_WINDOW_MS 15
#endif

static const char *TAG = "MenuManager";

typedef struct {
    IMenuManager base;
    _Atomic ServiceState serviceState;
    MenuManagerConfig config;
    MenuVisibilityContext visibility;
    
    QueueHandle_t eventQueue;
    WatchdogWorker worker;
    SemaphoreHandle_t controlMutex;
    SemaphoreHandle_t mutex;
    atomic_bool publicConfirmationActive;
    
    atomic_bool isEditing;
    atomic_bool confirmSelection; // false = No/Cancel, true = Yes/Confirm
    _Atomic uint32_t lastActivityTime;
    uint64_t lastActivityTimeMs; // Protected by activityLock.
    uint64_t lastButtonPressTime; // Cooldown/debounce timer for button presses
    uint64_t lastEncoderTimeMs;   // Timestamp for encoder step deduplication
    int8_t lastEncoderDirection;  // Direction of last processed encoder step
} MenuManagerImpl;

static MenuManagerImpl impl;
static portMUX_TYPE activityLock = portMUX_INITIALIZER_UNLOCKED;

/* Never hold this short critical section while publishing or taking a mutex. */
static void RecordActivity(void) {
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000ULL;
    portENTER_CRITICAL(&activityLock);
    if (now >= impl.lastActivityTimeMs) {
        impl.lastActivityTimeMs = now;
        impl.lastActivityTime = (uint32_t)now;
    }
    portEXIT_CRITICAL(&activityLock);
}
static uint64_t GetLastActivityTimeMs(const IMenuManager *self) {
    if (self != &impl.base) return 0;
    portENTER_CRITICAL(&activityLock);
    uint64_t value = impl.lastActivityTimeMs;
    portEXIT_CRITICAL(&activityLock);
    return value;
}


/* Forward declarations */
static void UpdateActivity(IMenuManager *self);

/* ──────────────────────────────────────────
 * Internal Helpers
 * ────────────────────────────────────────── */

static MenuManagerError PostEvent(IMenuManager *self, const MenuEvent *event) {
    if (self != &impl.base || !event) return MENU_ERR_NULL_PTR;
    if (event->type != MENU_EVENT_ENCODER_ROTATED && event->type != MENU_EVENT_BUTTON_PRESSED &&
        event->type != MENU_EVENT_NAVIGATE_TO && event->type != MENU_EVENT_WAKE_UP &&
        event->type != MENU_EVENT_BUTTON_LONG_PRESSED) return MENU_ERR_INVALID_STATE;
    if ((event->type == MENU_EVENT_NAVIGATE_TO && (unsigned)event->data.targetState >= UI_STATE_COUNT) ||
        (event->type == MENU_EVENT_ENCODER_ROTATED && !event->data.encoderDirection)) return MENU_ERR_INVALID_STATE;
    if (!impl.controlMutex) return MENU_ERR_NOT_INITIALIZED;
    if (xSemaphoreTake(impl.controlMutex, 0) != pdTRUE) return MENU_ERR_BUSY;
    MenuManagerError result = MENU_ERR_INVALID_STATE;
    if (impl.serviceState == SERVICE_STATE_STARTED && impl.eventQueue) {
        result = xQueueSend(impl.eventQueue, event, 0) == pdPASS ? MENU_OK : MENU_ERR_QUEUE_FULL;
    }
    xSemaphoreGive(impl.controlMutex);
    return result;
}

static void SendEvent(MenuEventType type, int8_t direction, UIState target) {
    MenuEvent event = {.type = type};
    if (type == MENU_EVENT_ENCODER_ROTATED) event.data.encoderDirection = direction;
    else if (type == MENU_EVENT_NAVIGATE_TO) event.data.targetState = target;
    MenuManagerError result = PostEvent(&impl.base, &event);
    if (result == MENU_ERR_QUEUE_FULL) ESP_LOGW(TAG, "Event queue full, dropping event %d", type);
}

static void NotifyStateChanged(UIState oldState, UIState newState) {
    if(MenuEditSession_State()!=newState)MenuEditSession_Cancel();
    IEventBus *bus = ServiceLocator_GetEventBus();
    RecordActivity();
    if (bus) {
        BusEvent e = {0};
        e.eventName = EVENT_MENU_STATE_CHANGED;
        bus->Publish(e);
        bus->Publish((BusEvent){.eventName = EVENT_MENU_ACTIVITY});
    }
}

/**
 * @brief Flush accumulated PCNT counts from the rotary encoder hardware.
 *
 * Called after every button-triggered navigation to discard any vibration
 * counts produced by the physical button switch actuation. Without this,
 * mechanical noise accumulates in the PCNT peripheral and fires as a
 * phantom rotation step once the button-cooldown window expires.
 */
static void FlushEncoderCounts(void) {
    IRotaryEncoder *enc =
        (IRotaryEncoder *)ServiceLocator_GetService(SVC_NAME_ROTARY_ENCODER);
    if (enc) {
        enc->Reset(enc);
        ESP_LOGD(TAG, "[NAV] Encoder PCNT flushed after button transition");
    }
}

/* ──────────────────────────────────────────
 * Core Logic (Runs in Task Context)
 * ────────────────────────────────────────── */

/* Read other owners before taking the menu lock; no configuration->menu lock cycle. */
static void RefreshVisibility(void) {
    IConfigurationManager *owner=(IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    SystemSettings settings;
    if(!owner || !owner->GetSnapshot || owner->GetSnapshot(owner,&settings)!=ESP_OK) return;
    MenuEditSession_Merge(&settings);
    IZigbee *zigbee=(IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    bool connected=zigbee && zigbee->IsConnected && zigbee->IsConnected(zigbee)>0;
    MenuVisibilityContext context=MenuNavigator_Context(&settings,connected);
    if(!impl.mutex || xSemaphoreTake(impl.mutex,portMAX_DELAY)!=pdTRUE) return;
    impl.visibility=context;
    UIState current=MenuStateMachine_GetCurrentState();
    if(!MenuNavigator_IsVisible(current,&context)) {
        UIState replacement=MenuNavigator_FirstVisible(MenuNavigator_GetParent(current),&context);
        if(current==UI_STATE_OBJECT_SOURCE || current==UI_STATE_AMBIENT_SOURCE ||
            current==UI_STATE_SET_LOW_TEMP || current==UI_STATE_SET_HIGH_TEMP ||
            current==UI_STATE_FAN_DELTA_ON || current==UI_STATE_FAN_DELTA_OFF) replacement=UI_STATE_FAN_MODE;
        if(current==UI_STATE_CAT_ZIGBEE_REP || (current>=UI_STATE_ZB_REP_FAN && current<=UI_STATE_ZB_REP_UPTIME)) {
            /* Leave the disappearing report category without retaining a stale back target. */
            if(MenuStateMachine_PeekHistory()==UI_STATE_CAT_ZIGBEE_REP) MenuStateMachine_PopHistory();
            replacement=UI_STATE_ZIGBEE_PAIR;
        }
        if(current==UI_STATE_CAT_RGB_XY || (current>=UI_STATE_RGB_COLD_X && current<=UI_STATE_RGB_FREEZE_Y)) {
            if(MenuStateMachine_PeekHistory()==UI_STATE_CAT_RGB_XY) MenuStateMachine_PopHistory();
            replacement=UI_STATE_SET_LED_MODE;
        }
        if(replacement==UI_STATE_COUNT) replacement=UI_STATE_MONITOR;
        UIState ancestor=MenuNavigator_GetParent(current);
        while((unsigned)ancestor<UI_STATE_COUNT && !MenuNavigator_IsVisible(ancestor,&context)) {
            if(MenuStateMachine_PeekHistory()==ancestor)MenuStateMachine_PopHistory();
            ancestor=MenuNavigator_GetParent(ancestor);
            replacement=MenuNavigator_FirstVisible(ancestor,&context);
        }
        if(replacement==UI_STATE_COUNT)replacement=UI_STATE_MONITOR;
        if(impl.isEditing) {
            impl.isEditing=false;
            MenuEditSession_Cancel();
            impl.confirmSelection=false;
            IEventBus *bus=ServiceLocator_GetEventBus();
            if(bus) bus->Publish((BusEvent){.eventName=EVENT_MENU_EDITING_FINISHED});
        }
        MenuStateMachine_TransitionTo(replacement);
        /* Configuration reconciliation is not a user gesture: do not extend inactivity. */
        IEventBus *bus=ServiceLocator_GetEventBus();
        if(bus) bus->Publish((BusEvent){.eventName=EVENT_MENU_STATE_CHANGED});
    }
    xSemaphoreGive(impl.mutex);
}

static void HandleEncoder(int8_t direction) {
    static uint32_t s_called = 0;
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000ULL;
    ESP_LOGI(TAG, "[DIAG-MM] HandleEncoder #%lu dir=%+d t=%llums",
             (unsigned long)++s_called, (int)direction, (unsigned long long)now);
    if (!impl.mutex || xSemaphoreTake(impl.mutex, portMAX_DELAY) != pdTRUE) return;


    UIState current = MenuStateMachine_GetCurrentState();
    const char *curLabel = MenuManager_GetStateLabel(current);

    uint64_t sinceBtn = (now >= impl.lastButtonPressTime) ? (now - impl.lastButtonPressTime) : 0;
    if (sinceBtn < RE_BUTTON_ROTATION_COOLDOWN_MS) {
        ESP_LOGW(TAG, "[NAV] ENC dir=%+d BLOCKED (btn cooldown %ums remaining) state=%d(%s)",
                 (int)direction, (unsigned)(RE_BUTTON_ROTATION_COOLDOWN_MS - sinceBtn),
                 (int)current, curLabel ? curLabel : "?");
        xSemaphoreGive(impl.mutex);
        return;
    }

    // --- Dedup guard ---
    uint64_t sinceEnc = (now >= impl.lastEncoderTimeMs) ? (now - impl.lastEncoderTimeMs) : 0;
    if (direction != 0 && sinceEnc < RE_ENCODER_DEDUP_WINDOW_MS && direction == impl.lastEncoderDirection) {
        ESP_LOGW(TAG, "[NAV] ENC dir=%+d DEDUP (same dir within %ums) state=%d(%s)",
                 (int)direction, (unsigned)sinceEnc,
                 (int)current, curLabel ? curLabel : "?");
        xSemaphoreGive(impl.mutex);
        return;
    }
    impl.lastEncoderTimeMs = now;
    impl.lastEncoderDirection = direction;
    RecordActivity();

    ESP_LOGI(TAG, "[NAV] ENC dir=%+d | state=%d(%s) | btnAge=%llums encAge=%llums",
             (int)direction, (int)current, curLabel ? curLabel : "?",
             (unsigned long long)sinceBtn, (unsigned long long)sinceEnc);

    // When editing a setting value/action, dispatch encoder rotation to plugin
    if (impl.isEditing) {
        MenuEditSession_ReportResult(current,ESP_OK);
        if (MenuStateMachine_RequiresConfirmation(current)) {
            impl.confirmSelection = !impl.confirmSelection;
            xSemaphoreGive(impl.mutex);
            return;
        }
        MenuPluginEvent pEvent = {
            .state = current,
            .action = MENU_PLUGIN_ACTION_ENCODER,
            .direction = direction,
            .handled = false
        };
        
        if (MenuPluginManager_DispatchEvent(&pEvent)) {
            xSemaphoreGive(impl.mutex);
            return;
        }

    } else {
        // Navigation
        UIState next = current;

        /* Determine the parent context from the navigation history so that
         * shared nodes (especially UI_STATE_BACK, which appears in every
         * category's child array) are resolved within the correct category
         * rather than the last one that happened to overwrite nodeRegistry. */
        UIState parentContext = MenuStateMachine_PeekHistory();

        if (current == UI_STATE_MONITOR) {
            // Main monitor screen: rotation does nothing (click-only entry)
            ESP_LOGI(TAG, "[NAV] ENC ignored on MONITOR (click to enter menu)");
        } else if (current == UI_STATE_MENU_ROOT) {
            // Menu open, nothing selected yet: select first or last root child
            if (direction > 0) {
                next = MenuNavigator_FirstVisible(UI_STATE_MONITOR,&impl.visibility);
            } else {
                next = MenuNavigator_LastVisible(UI_STATE_MONITOR,&impl.visibility);
            }
        } else {
            if (direction > 0) {
                next = MenuNavigator_NextVisible(current,parentContext,impl.config.circularNavigation,&impl.visibility);
            } else {
                next = MenuNavigator_PreviousVisible(current,parentContext,impl.config.circularNavigation,&impl.visibility);
            }
        }

        const char *nextLabel = MenuManager_GetStateLabel(next);
        ESP_LOGI(TAG, "[NAV] ENC result: %d(%s) -> %d(%s)%s",
                 (int)current, curLabel ? curLabel : "?",
                 (int)next, nextLabel ? nextLabel : "?",
                 (next == current) ? " [NO CHANGE]" : "");

        if (next != current) {
            MenuNavigator_RememberFocus(parentContext,next);
            UIState old = current;
            if (MenuStateMachine_TransitionTo(next)) {
                NotifyStateChanged(old, next);
            } else {
                ESP_LOGW(TAG, "[NAV] Transition %d->%d REJECTED by state machine", (int)old, (int)next);
            }
        }
    }
    xSemaphoreGive(impl.mutex);
}

static UIState HandleButton_Unlocked(void) {
    UIState confirmedAction = UI_STATE_COUNT;
    UIState current = MenuStateMachine_GetCurrentState();
    if(!MenuNavigator_IsVisible(current,&impl.visibility)) return UI_STATE_COUNT;
    const char *curLabel = MenuManager_GetStateLabel(current);
    MenuNodeType type = MenuNavigator_GetNodeType(current);

    ESP_LOGI(TAG, "[NAV] BTN | state=%d(%s) | nodeType=%d | isEditing=%d",
             (int)current, curLabel ? curLabel : "?", (int)type, (int)impl.isEditing);

    if (impl.isEditing) {
        if(MenuEditSession_State()==current) return current;
        /* Range button phases and checked Save run after the menu lock is released. */
        if (MenuRangeEditor_Zone(current)>=0 || RGBZoneSelection_Zone(current)>=0) return current;
        if (current == UI_STATE_ZIGBEE_PAIR) {
            ESP_LOGI(TAG, "[PAIRING] In Zigbee Pairing screen -> Publishing MENU_EVENT_ZIGBEE_ACTION to EventBus");
            IEventBus *bus = (IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
            if (bus) {
                bus->Publish((BusEvent){.eventName = "MENU_EVENT_ZIGBEE_ACTION", .type = EVENT_TYPE_INFO});
            }
            return UI_STATE_COUNT; // Keep pairing screen active
        }

        if (RGBZoneSelection_Zone(current) >= 0 || current==UI_STATE_SET_LED_ANIM ||
            current==UI_STATE_SET_BUILTIN_STARTUP || current==UI_STATE_SET_BUILTIN_IDLE ||
            current==UI_STATE_SET_BUILTIN_CONNECTING || current==UI_STATE_SET_BUILTIN_FAULT) confirmedAction = current;
        // Finish editing or confirm action
        if (MenuStateMachine_RequiresConfirmation(current)) {
            if (impl.confirmSelection) {
                ESP_LOGI(TAG, "[NAV] BTN confirm=YES for state=%d(%s)", (int)current, curLabel ? curLabel : "?");
                confirmedAction = current;
                IEventBus *bus = ServiceLocator_GetEventBus();
                if (bus) bus->Publish((BusEvent){.eventName = EVENT_MENU_CONFIRM_ACCEPTED});
            } else {
                ESP_LOGI(TAG, "[NAV] BTN confirm=NO (cancel) for state=%d(%s)", (int)current, curLabel ? curLabel : "?");
                MenuStateMachine_PopHistory();
            }
        }
        impl.isEditing = false;
        IEventBus *bus = ServiceLocator_GetEventBus();
        if (bus) bus->Publish((BusEvent){.eventName = EVENT_MENU_EDITING_FINISHED});
    } else {
        // Enter sub-menu, perform action, or start editing
        if (current == UI_STATE_MENU_ROOT) {
            // Button in unselected menu root: enter first root child (CAT_TEMP)
            UIState firstCat = MenuNavigator_FirstVisible(UI_STATE_MONITOR,&impl.visibility);
            if (firstCat != UI_STATE_COUNT) {
                const char *catLabel = MenuManager_GetStateLabel(firstCat);
                ESP_LOGI(TAG, "[NAV] BTN MENU_ROOT -> first category %d(%s)",
                         (int)firstCat, catLabel ? catLabel : "?");
                MenuStateMachine_TransitionTo(firstCat);
                impl.lastEncoderTimeMs = 0;
                impl.lastEncoderDirection = 0;
                FlushEncoderCounts();
                NotifyStateChanged(current, firstCat);
            }
        } else if (current == UI_STATE_MONITOR) {
            // Button on monitor: open menu directly on first category (CAT_TEMP)
            UIState firstCat = MenuNavigator_FirstVisible(UI_STATE_MONITOR,&impl.visibility);
            if (firstCat == UI_STATE_COUNT) {
                firstCat = UI_STATE_CAT_TEMP;
            }
            const char *catLabel = MenuManager_GetStateLabel(firstCat);
            ESP_LOGI(TAG, "[NAV] BTN MONITOR -> open menu on %d(%s)",
                     (int)firstCat, catLabel ? catLabel : "?");
            if (!MenuStateMachine_PushHistory()) return UI_STATE_COUNT;
            MenuStateMachine_TransitionTo(firstCat);
            impl.lastEncoderTimeMs = 0;
            impl.lastEncoderDirection = 0;
            FlushEncoderCounts();
            NotifyStateChanged(current, firstCat);
        } else if (type == MENU_NODE_CATEGORY || type == MENU_NODE_ROOT) {
            UIState child = MenuNavigator_PreferredChild(current,&impl.visibility);
            if (child != UI_STATE_COUNT) {
                const char *childLabel = MenuManager_GetStateLabel(child);
                ESP_LOGI(TAG, "[NAV] BTN CATEGORY %d(%s) -> first child %d(%s)",
                         (int)current, curLabel ? curLabel : "?",
                         (int)child, childLabel ? childLabel : "?");
                if (!MenuStateMachine_PushHistory()) return UI_STATE_COUNT;
                MenuStateMachine_TransitionTo(child);
                impl.lastEncoderTimeMs = 0;
                impl.lastEncoderDirection = 0;
                FlushEncoderCounts();
                NotifyStateChanged(current, child);
            } else {
                ESP_LOGW(TAG, "[NAV] BTN CATEGORY %d(%s) has no children", (int)current, curLabel ? curLabel : "?");
            }
        } else if (type == MENU_NODE_BACK) {
            UIState prev = MenuStateMachine_PopHistory();
            const char *prevLabel = MenuManager_GetStateLabel(prev);
            ESP_LOGI(TAG, "[NAV] BTN BACK -> %d(%s)", (int)prev, prevLabel ? prevLabel : "?");
            impl.lastEncoderTimeMs = 0;
            impl.lastEncoderDirection = 0;
            FlushEncoderCounts();
            NotifyStateChanged(current, prev);
        } else if (type == MENU_NODE_EXIT) {
            ESP_LOGI(TAG, "[NAV] BTN EXIT -> MONITOR");
            MenuStateMachine_TransitionTo(UI_STATE_MONITOR);
            MenuStateMachine_ClearHistory();
            impl.lastEncoderTimeMs = 0;
            impl.lastEncoderDirection = 0;
            FlushEncoderCounts();
            NotifyStateChanged(current, UI_STATE_MONITOR);
        } else if (type == MENU_NODE_LEAF || type == MENU_NODE_ACTION) {
            ESP_LOGI(TAG, "[NAV] BTN LEAF/ACTION %d(%s) -> start editing", (int)current, curLabel ? curLabel : "?");
            impl.isEditing = true;
            impl.confirmSelection = false;
            if (RGBZoneSelection_Zone(current) >= 0 || MenuRangeEditor_Zone(current)>=0) {
                MenuPluginEvent begin = {.state=current,.action=MENU_PLUGIN_ACTION_ENCODER,.direction=0};
                MenuPluginManager_DispatchEvent(&begin);
            }
            IEventBus *bus = ServiceLocator_GetEventBus();
            if (bus) bus->Publish((BusEvent){.eventName = EVENT_MENU_EDITING_STARTED});
        } else {
            ESP_LOGW(TAG, "[NAV] BTN unhandled nodeType=%d state=%d(%s)", (int)type, (int)current, curLabel ? curLabel : "?");
        }
    }
    return confirmedAction;
}

static void DispatchConfirmedAction(UIState action) {
    if ((unsigned)action >= UI_STATE_COUNT) return;
    if(MenuEditSession_State()==action && !MenuEditSession_AutoCommit()) {
        UIState range=action==UI_STATE_FAN_MODE ? MenuEditSession_FanRange() : UI_STATE_COUNT;
        if(range!=UI_STATE_COUNT) {
            if(!impl.mutex || xSemaphoreTake(impl.mutex,portMAX_DELAY)!=pdTRUE) {MenuEditSession_Cancel();return;}
            MenuStateMachine_TransitionTo(range);
            NotifyStateChanged(action,range);
            xSemaphoreGive(impl.mutex);
            MenuPluginEvent begin={.state=range,.action=MENU_PLUGIN_ACTION_ENCODER,.direction=0};
            MenuPluginManager_DispatchEvent(&begin);
            return;
        }
        esp_err_t result=MenuEditSession_Commit();
        MenuEditSession_ReportResult(action,result);
        if(result!=ESP_OK) {ESP_LOGE(TAG,"Menu save rejected: state=%d result=0x%x",action,(unsigned)result);return;}
        MenuPluginEvent applied={.state=action,.action=MENU_PLUGIN_ACTION_BUTTON};
        MenuPluginManager_DispatchEvent(&applied);
        if(MenuEditSession_Result(action)!=ESP_OK) {
            esp_err_t failure=MenuEditSession_Result(action);
            IConfigurationManager *owner=ConfigurationManager_GetInstance();SystemSettings current;
            if(owner && owner->GetSnapshot && owner->GetSnapshot(owner,&current)==ESP_OK)MenuEditSession_Begin(action,owner,&current);
            MenuEditSession_ReportResult(action,failure);
            return;
        }
        impl.base.SetEditing(&impl.base,false);
        IEventBus *bus=ServiceLocator_GetEventBus();
        if(bus && bus->Publish)bus->Publish((BusEvent){.eventName=EVENT_MENU_EDITING_FINISHED});
        return;
    }
    MenuPluginEvent event = {.state = action, .action = MENU_PLUGIN_ACTION_BUTTON};
    MenuPluginManager_DispatchEvent(&event);
}

static void HandleButton(void) {
    /* Owner snapshot is acquired before the menu lock. Only the worker opens drafts. */
    bool opened=false;
    UIState current=MenuStateMachine_GetCurrentState();
    if(!impl.isEditing && MenuEditSession_Supports(current)) {
        IConfigurationManager *owner=ConfigurationManager_GetInstance();SystemSettings snapshot;
        if(!owner || !owner->GetSnapshot || owner->GetSnapshot(owner,&snapshot)!=ESP_OK ||
           !MenuEditSession_Begin(current,owner,&snapshot))return;
        opened=true;
    }
    if (!impl.mutex || xSemaphoreTake(impl.mutex, portMAX_DELAY) != pdTRUE) {if(opened)MenuEditSession_Cancel();return;}
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000ULL;
    if (now - impl.lastButtonPressTime < MENU_BUTTON_COOLDOWN_MS) {
        ESP_LOGD(TAG, "Button press debounced");
        xSemaphoreGive(impl.mutex);
        if(opened)MenuEditSession_Cancel();
        return;
    }
    impl.lastButtonPressTime = now;
    UIState action = HandleButton_Unlocked();
    xSemaphoreGive(impl.mutex);
    DispatchConfirmedAction(action);
}

/* Reconstruct category history for diagnostic navigation and long-press entry. */
static bool NavigateBranch(UIState target) {
    if(target==UI_STATE_BACK)return MenuStateMachine_TransitionTo(target);
    UIState path[MENU_HISTORY_CAPACITY];unsigned count=0;
    UIState parent=target;
    while(parent!=UI_STATE_MONITOR) {
        parent=MenuNavigator_GetParent(parent);
        if((unsigned)parent>=UI_STATE_COUNT || count==MENU_HISTORY_CAPACITY)return false;
        for(unsigned i=0;i<count;++i)if(path[i]==parent)return false;
        path[count++]=parent;
    }
    MenuStateMachine_ClearHistory();
    while(count) {
        MenuStateMachine_ForceState(path[--count]);
        if(!MenuStateMachine_PushHistory())return false;
    }
    return MenuStateMachine_TransitionTo(target);
}

static void MenuTask(void *params) {
    MenuEvent event;
    ESP_LOGI(TAG, "Menu Task started");
    
    for (;;) {
        WatchdogWorker_Checkpoint(&impl.worker, NULL, (uint64_t)(esp_timer_get_time() / 1000));
        TickType_t wait = pdMS_TO_TICKS(MENU_WORKER_POLL_MS);
        if (!wait) wait = 1;
        bool received=xQueueReceive(impl.eventQueue,&event,wait)==pdPASS;
        RefreshVisibility();
        if (received) {
            WatchdogWorker_Checkpoint(&impl.worker, NULL, (uint64_t)(esp_timer_get_time() / 1000));
            switch (event.type) {
                case MENU_EVENT_ENCODER_ROTATED:
                    HandleEncoder(event.data.encoderDirection);
                    break;
                case MENU_EVENT_BUTTON_PRESSED:
                    HandleButton();
                    break;
                case MENU_EVENT_WAKE_UP:
                case MENU_EVENT_BUTTON_LONG_PRESSED:
                case MENU_EVENT_NAVIGATE_TO:
                    MenuEditSession_Cancel();
                    MenuRangeEditor_Clear();
                    if (event.type == MENU_EVENT_WAKE_UP) event.data.targetState = UI_STATE_MONITOR;
                    if (event.type == MENU_EVENT_BUTTON_LONG_PRESSED) event.data.targetState = UI_STATE_ZIGBEE_PAIR;
                    if (!impl.mutex || xSemaphoreTake(impl.mutex, portMAX_DELAY) != pdTRUE) break;
                    impl.lastButtonPressTime = (uint64_t)esp_timer_get_time() / 1000ULL;
                    UIState previous = MenuStateMachine_GetCurrentState();
                    impl.isEditing = event.type == MENU_EVENT_BUTTON_LONG_PRESSED;
                    impl.confirmSelection = false;
                    if (event.data.targetState == UI_STATE_MONITOR) {
                        MenuStateMachine_ClearHistory();
                    }
                    UIState target=event.data.targetState;
                    if(!MenuNavigator_IsVisible(target,&impl.visibility)) target=UI_STATE_CAT_TEMP;
                    if(NavigateBranch(target))NotifyStateChanged(previous,target);
                    xSemaphoreGive(impl.mutex);
                    break;
                default:
                    break;
            }
        }
    }
}

/* ──────────────────────────────────────────
 * IMenuManager Interface Implementation
 * ────────────────────────────────────────── */

static void ProcessEncoder(IMenuManager *self, int8_t direction) {
    UpdateActivity(self);
    SendEvent(MENU_EVENT_ENCODER_ROTATED, direction, 0);
}

static void ProcessButton(IMenuManager *self) {
    UpdateActivity(self);
    SendEvent(MENU_EVENT_BUTTON_PRESSED, 0, 0);
}

static void ProcessLongPress(IMenuManager *self) {
    UpdateActivity(self);
    SendEvent(MENU_EVENT_BUTTON_LONG_PRESSED, 0, 0);
}

static UIState GetCurrentState(const IMenuManager *self) {
    return MenuStateMachine_GetCurrentState();
}

static bool IsEditing(const IMenuManager *self) {
    return impl.isEditing;
}

static void SetEditing(IMenuManager *self, bool editing) {
    if (!impl.mutex || xSemaphoreTake(impl.mutex, portMAX_DELAY) != pdTRUE) return;
    impl.isEditing = editing;
    if(!editing)MenuEditSession_Cancel();
    xSemaphoreGive(impl.mutex);
}

static UIState GetActiveCategory(const IMenuManager *self) {
    MenuStateSnapshot snapshot = MenuStateMachine_GetSnapshot();
    UIState current = snapshot.currentState;
    
    // If we are at the monitor or menu root or top category selection, the active category is root
    if (current == UI_STATE_MONITOR || current == UI_STATE_MENU_ROOT) {
        return UI_STATE_MONITOR;
    }
    
    // If the node itself is a top-level category or exit, we are at the main category level
    if (current == UI_STATE_CAT_TEMP || current == UI_STATE_CAT_LIGHT ||
        current == UI_STATE_CAT_DEVICE || current == UI_STATE_CAT_POWER ||
        current == UI_STATE_EXIT) {
        return UI_STATE_MONITOR;
    }
    
    // If history has a valid category context pushed, prefer history
    UIState hist = snapshot.historyParent;
    if (hist != UI_STATE_MONITOR && hist != UI_STATE_COUNT) {
        return hist;
    }
    
    // Fallback to static parent registry
    return MenuNavigator_GetParent(current);
}

static uint32_t GetLastActivityTime(const IMenuManager *self) {
    return impl.lastActivityTime;
}

static void SetState(IMenuManager *self, UIState state) {
    SendEvent(MENU_EVENT_NAVIGATE_TO, 0, state);
}

static void UpdateActivity(IMenuManager *self) {
    RecordActivity();
    IEventBus *bus = ServiceLocator_GetEventBus();
    if (bus) {
        bus->Publish((BusEvent){.eventName = EVENT_MENU_ACTIVITY});
    }
}

static bool CopyLabel(const IMenuManager *self, UIState state, char *output, size_t capacity) {
    if (self != &impl.base || (unsigned)state >= UI_STATE_COUNT || !output || !capacity) return false;
    char label[MENU_LABEL_CAPACITY];
    const char *source = label;
    if (!MenuPluginManager_CopyLabel(state, label, sizeof(label)))
        source = MenuManager_GetStateLabel(state);
    if (!source) return false;
    size_t length = strlen(source);
    if (capacity <= length) return false;
    memcpy(output, source, length + 1);
    return true;
}

static MenuManagerError RegisterPluginEx(IMenuManager *self, const char *label, UIState state, MenuPluginHandlerEx handler) {
    return MenuPluginManager_Register(state, label, handler);
}

static MenuManagerError RegisterPlugin(IMenuManager *self, const char *label, UIState state, LegacyMenuPluginAction onAction) {
    return MenuPluginManager_RegisterLegacy(state, label, onAction);
}

static MenuManagerError UnregisterPlugin(IMenuManager *self, UIState state) {
    return MenuPluginManager_Unregister(state);
}

static MenuManagerError RegisterPluginBatch(IMenuManager *self, const MenuPluginRegistration *entries, size_t count) {
    if (self != &impl.base) return MENU_ERR_INVALID_STATE;
    return MenuPluginManager_RegisterBatch(entries, count);
}

static MenuManagerError UnregisterPluginBatch(IMenuManager *self, const MenuPluginRegistration *entries, size_t count) {
    if (self != &impl.base) return MENU_ERR_INVALID_STATE;
    return MenuPluginManager_UnregisterBatch(entries, count);
}

static void GoBack(IMenuManager *self) {
    if (!impl.mutex || xSemaphoreTake(impl.mutex, portMAX_DELAY) != pdTRUE) return;
    MenuStateMachine_PopHistory();
    xSemaphoreGive(impl.mutex);
}

static void ConfirmAction(IMenuManager *self, bool accept) {
    if (self != &impl.base || !impl.mutex || !impl.controlMutex) return;
    /* Admit one synchronous public confirmation under lifecycle ownership.
     * Never retain the control lock while waiting for state or user code. */
    if (xSemaphoreTake(impl.controlMutex, 0) != pdTRUE) return;
    if (impl.serviceState != SERVICE_STATE_STARTED || atomic_load(&impl.publicConfirmationActive)) {
        xSemaphoreGive(impl.controlMutex);
        return;
    }
    atomic_store(&impl.publicConfirmationActive, true);
    xSemaphoreGive(impl.controlMutex);
    if (xSemaphoreTake(impl.mutex, portMAX_DELAY) != pdTRUE) {
        atomic_store(&impl.publicConfirmationActive, false);
        return;
    }
    UIState action = UI_STATE_COUNT;
    if (impl.isEditing && MenuStateMachine_RequiresConfirmation(MenuStateMachine_GetCurrentState())) {
        impl.confirmSelection = accept;
        action = HandleButton_Unlocked();
    }
    xSemaphoreGive(impl.mutex);
    DispatchConfirmedAction(action);
    atomic_store(&impl.publicConfirmationActive, false);
}

static bool GetConfirmSelection(const IMenuManager *self) {
    return impl.confirmSelection;
}

/* ──────────────────────────────────────────
 * IService Lifecycle
 * ────────────────────────────────────────── */

static int32_t InitializeWithConfig(IMenuManager *self, const MenuManagerConfig *config, struct IDIRegistry *registry) {
    if (self != &impl.base) return -1;
    if (impl.serviceState != SERVICE_STATE_UNINITIALIZED) return 0;

    MenuManagerConfig requested = config ? *config : MenuManager_GetDefaultConfig();
    if (requested.eventQueueSize < MENU_QUEUE_CAPACITY_MIN || requested.eventQueueSize > MENU_QUEUE_CAPACITY_MAX ||
        requested.taskStackSize < MENU_TASK_STACK_MIN_BYTES || requested.taskStackSize > MENU_TASK_STACK_MAX_BYTES ||
        requested.taskPriority >= configMAX_PRIORITIES) return -1;

    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (!mutex) return -1;
    QueueHandle_t queue = xQueueCreate(requested.eventQueueSize, sizeof(MenuEvent));
    if (!queue) {
        vSemaphoreDelete(mutex);
        return -1;
    }
    SemaphoreHandle_t control = xSemaphoreCreateMutex();
    if (!control || WatchdogWorker_Init(&impl.worker) != 0) {
        if (control) vSemaphoreDelete(control);
        vQueueDelete(queue);
        vSemaphoreDelete(mutex);
        return -1;
    }
    impl.config = requested;
    impl.mutex = mutex;
    impl.eventQueue = queue;
    impl.controlMutex = control;
    
    MenuNavigator_Init();
    MenuEditSession_Cancel();
    MenuStateMachine_Init();
    MenuPluginManager_Init();
    
    impl.serviceState = SERVICE_STATE_INITIALIZED;
    ESP_LOGI(TAG, "Initialized with config");
    return 0;
}

static int32_t Initialize(IService *self, IDIRegistry *registry) {
    return InitializeWithConfig((IMenuManager*)self, NULL, registry);
}

static int32_t Start(IService *self) {
    if (self != &impl.base.base || !impl.controlMutex) return -1;
    if (xSemaphoreTake(impl.controlMutex, pdMS_TO_TICKS(MENU_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    ServiceState previous = impl.serviceState;
    if (previous != SERVICE_STATE_INITIALIZED && previous != SERVICE_STATE_STOPPED) {
        xSemaphoreGive(impl.controlMutex);
        return -1;
    }
    
    // Start the inactivity deadline when the worker is launched.
    RecordActivity();
    
    impl.serviceState = SERVICE_STATE_STARTED;
    if (WatchdogWorker_Start(&impl.worker, MenuTask, "MenuTask", impl.config.taskStackSize,
                            NULL, impl.config.taskPriority) != 0) {
        impl.serviceState = previous;
        xSemaphoreGive(impl.controlMutex);
        return -1;
    }
    xSemaphoreGive(impl.controlMutex);
    
    ESP_LOGI(TAG, "Started");
    return 0;
}

static int32_t Stop(IService *self) {
    if (self != &impl.base.base || !impl.controlMutex) return -1;
    TaskHandle_t worker = atomic_load(&impl.worker.task);
    if (worker && xTaskGetCurrentTaskHandle() == worker) return -1;
    if (xSemaphoreTake(impl.controlMutex, pdMS_TO_TICKS(MENU_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    /* The worker never takes controlMutex. Wait outside the menu state mutex,
     * including when a plugin callback has not returned yet. */
    impl.serviceState = SERVICE_STATE_ERROR;
    /* A public callback can itself request Stop. Return busy instead of
     * waiting for that same call or reporting a false shutdown barrier. */
    if (atomic_load(&impl.publicConfirmationActive)) {
        xSemaphoreGive(impl.controlMutex);
        return -1;
    }
    int32_t result = WatchdogWorker_Stop(&impl.worker, pdMS_TO_TICKS(MENU_CONTROL_TIMEOUT_MS));
    if (result == 0) {
        MenuEvent discarded;
        while (xQueueReceive(impl.eventQueue, &discarded, 0) == pdPASS) {}
        MenuEditSession_Cancel();
        impl.serviceState = SERVICE_STATE_STOPPED;
    }
    xSemaphoreGive(impl.controlMutex);
    return result;
}

static int32_t CheckHealth(IService *self) {
    if (self != &impl.base.base || impl.serviceState != SERVICE_STATE_STARTED ||
        !impl.eventQueue || !impl.controlMutex || !impl.mutex) return -1;
    return WatchdogWorker_Healthy(&impl.worker) ? 0 : -1;
}

static ServiceState GetState(IService *self) {
    return impl.serviceState;
}

/* ──────────────────────────────────────────
 * Singleton Access
 * ────────────────────────────────────────── */

IMenuManager* GetMenuManager(void) {
    static bool singleton_initialized = false;
    if (!singleton_initialized) {
        impl.base.base.Initialize = Initialize;
        impl.base.base.Start = Start;
        impl.base.base.Stop = Stop;
        impl.base.base.GetState = GetState;
        impl.base.base.CheckHealth = CheckHealth;
        
        impl.base.InitializeWithConfig = InitializeWithConfig;
        impl.base.PostEvent = PostEvent;
        impl.base.ProcessEncoder = ProcessEncoder;
        impl.base.ProcessButton = ProcessButton;
        impl.base.UpdateActivity = UpdateActivity;
        impl.base.GetLastActivityTime = GetLastActivityTime;
        impl.base.GetLastActivityTimeMs = GetLastActivityTimeMs;
        impl.base.SetState = SetState;
        impl.base.ProcessLongPress = ProcessLongPress;
        impl.base.GetCurrentState = GetCurrentState;
        impl.base.GetActiveCategory = GetActiveCategory;
        impl.base.CopyLabel = CopyLabel;
        impl.base.IsEditing = IsEditing;
        impl.base.SetEditing = SetEditing;
        impl.base.RegisterPluginEx = RegisterPluginEx;
        impl.base.RegisterPlugin = RegisterPlugin;
        impl.base.UnregisterPlugin = UnregisterPlugin;
        impl.base.RegisterPluginBatch = RegisterPluginBatch;
        impl.base.UnregisterPluginBatch = UnregisterPluginBatch;
        impl.base.GoBack = GoBack;
        impl.base.ConfirmAction = ConfirmAction;
        impl.base.GetConfirmSelection = GetConfirmSelection;
        
        impl.serviceState = SERVICE_STATE_UNINITIALIZED;
        singleton_initialized = true;
    }
    return &impl.base;
}

IMenuManager* MenuManager_GetModule(void) {
    return GetMenuManager();
}
