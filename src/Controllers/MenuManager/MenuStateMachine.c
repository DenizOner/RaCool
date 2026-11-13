#include "Controllers/MenuManager/MenuStateMachine.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "Controllers/MenuManager/MenuManagerConfig.h"

static const char* TAG __attribute__((unused)) = "MenuStateMachine";

typedef struct {
    UIState currentState;
    UIState history[MENU_HISTORY_CAPACITY];
    uint8_t historyCount;
} StateMachineContext;

static StateMachineContext smCtx;
static portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;

void MenuStateMachine_Init(void) {
    portENTER_CRITICAL(&stateLock);
    smCtx.currentState = UI_STATE_MONITOR;
    smCtx.historyCount = 0;
    portEXIT_CRITICAL(&stateLock);
}

MenuStateSnapshot MenuStateMachine_GetSnapshot(void) {
    portENTER_CRITICAL(&stateLock);
    MenuStateSnapshot snapshot = {
        .currentState = smCtx.currentState,
        .historyParent = smCtx.historyCount ? smCtx.history[smCtx.historyCount - 1] : UI_STATE_MONITOR
    };
    portEXIT_CRITICAL(&stateLock);
    return snapshot;
}

UIState MenuStateMachine_GetCurrentState(void) {
    return MenuStateMachine_GetSnapshot().currentState;
}

void MenuStateMachine_ForceState(UIState newState) {
    if ((unsigned)newState >= UI_STATE_COUNT) return;
    portENTER_CRITICAL(&stateLock);
    smCtx.currentState = newState;
    portEXIT_CRITICAL(&stateLock);
}

bool MenuStateMachine_TransitionTo(UIState newState) {
    if ((unsigned)newState >= UI_STATE_COUNT) return false;
    
    // Valid states transition directly; action confirmation is handled by MenuManager.
    portENTER_CRITICAL(&stateLock);
    smCtx.currentState = newState;
    portEXIT_CRITICAL(&stateLock);
    return true;
}

bool MenuStateMachine_PushHistory(void) {
    portENTER_CRITICAL(&stateLock);
    if (smCtx.historyCount < MENU_HISTORY_CAPACITY) {
        smCtx.history[smCtx.historyCount++] = smCtx.currentState;
        portEXIT_CRITICAL(&stateLock);
        return true;
    }
    portEXIT_CRITICAL(&stateLock);
    return false;
}

UIState MenuStateMachine_PopHistory(void) {
    portENTER_CRITICAL(&stateLock);
    smCtx.currentState = smCtx.historyCount ? smCtx.history[--smCtx.historyCount] : UI_STATE_MONITOR;
    UIState state = smCtx.currentState;
    portEXIT_CRITICAL(&stateLock);
    return state;
}

UIState MenuStateMachine_PeekHistory(void) {
    return MenuStateMachine_GetSnapshot().historyParent;
}

void MenuStateMachine_ClearHistory(void) {
    portENTER_CRITICAL(&stateLock);
    smCtx.historyCount = 0;
    portEXIT_CRITICAL(&stateLock);
}

bool MenuStateMachine_RequiresConfirmation(UIState state) {
    switch (state) {
        case UI_STATE_RESTART:
        case UI_STATE_SHUTDOWN:
        case UI_STATE_FACTORY_RESET:
            return true;
        default:
            return false;
    }
}
