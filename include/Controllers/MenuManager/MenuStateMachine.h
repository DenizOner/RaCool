#ifndef MENU_STATE_MACHINE_H
#define MENU_STATE_MACHINE_H

#include "Controllers/MenuManager/MenuManagerTypes.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Each operation uses a short internal critical section without callbacks.
 * Multiple calls are not a transaction; use GetSnapshot for a matching pair. */
typedef struct {
    UIState currentState;
    UIState historyParent;
} MenuStateSnapshot;

/* Reset before publishing the service. */
void MenuStateMachine_Init(void);
MenuStateSnapshot MenuStateMachine_GetSnapshot(void);
UIState MenuStateMachine_GetCurrentState(void);
/* Change state without changing history. Invalid states are rejected. */
void MenuStateMachine_ForceState(UIState newState);
bool MenuStateMachine_TransitionTo(UIState newState);
/* Bounded history discards the oldest entry when full. */
bool MenuStateMachine_PushHistory(void);
UIState MenuStateMachine_PopHistory(void);
UIState MenuStateMachine_PeekHistory(void);
void MenuStateMachine_ClearHistory(void);
bool MenuStateMachine_RequiresConfirmation(UIState state);

#ifdef __cplusplus
}
#endif

#endif // MENU_STATE_MACHINE_H
