#ifndef MENU_EDIT_SESSION_H
#define MENU_EDIT_SESSION_H
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"

/* One menu-worker writer. UI/visibility readers take a short snapshot lock.
 * No owner SDK/persistence call is made while holding the snapshot lock.
 * The facade is explicit and menu-local; other configuration clients keep
 * using the real owner. Cancel during an admitted save defers closure until
 * the owner call returns; its payload cannot be reused before then. Lifecycle
 * shutdown must drain the menu callback before destroying the owner. */
bool MenuEditSession_Supports(UIState state);
bool MenuEditSession_Begin(UIState state,IConfigurationManager *owner,const SystemSettings *snapshot);
IConfigurationManager *MenuEditSession_Config(IConfigurationManager *owner);
bool MenuEditSession_Overlay(SystemSettings *snapshot);
/* Merge only edited fields into a fresh live snapshot; unrelated owners remain visible. */
bool MenuEditSession_Merge(SystemSettings *snapshot);
UIState MenuEditSession_State(void);
bool MenuEditSession_AutoCommit(void);
UIState MenuEditSession_FanRange(void);
esp_err_t MenuEditSession_Commit(void);
void MenuEditSession_Cancel(void);
void MenuEditSession_ReportResult(UIState state,esp_err_t result);
esp_err_t MenuEditSession_Result(UIState state);
#endif
