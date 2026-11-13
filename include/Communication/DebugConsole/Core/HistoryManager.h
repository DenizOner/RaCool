#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "Communication/DebugConsole/DebugConsoleConfig.h"

/* Initialize before concurrent consumers; allocation failure remains retryable. */
bool HistoryManager_Init(void);
bool HistoryManager_Add(const char *cmd);
/* Legacy borrowed views are invalidated by concurrent mutations. Production
 * consumers must use the caller-owned Copy APIs below. */
const char *HistoryManager_GetPrevious(void);
const char *HistoryManager_GetNext(void);
const char *const *HistoryManager_GetAll(int *count);
void HistoryManager_ResetNavigation(void);
/* Return -1 if the count lock is unavailable. */
int HistoryManager_GetCount(void);
void HistoryManager_Clear(void);

/* Caller-owned snapshots remain valid after registry mutations. Failure leaves
 * output/count unchanged. Navigation returns an empty line at the newest end. */
bool HistoryManager_CopyAll(char (*output)[DEBUG_CONSOLE_LINE_BUFFER_SIZE], size_t capacity, int *count);
bool HistoryManager_CopyNavigation(bool previous, char *output, size_t capacity);

#endif /* HISTORY_MANAGER_H */
