#include "Communication/DebugConsole/Core/HistoryManager.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

/**
 * @brief Circular command history buffer.
 * Mutable storage is protected by its mutex.
 */
typedef struct {
    char entries[DEBUG_CONSOLE_HISTORY_SIZE][DEBUG_CONSOLE_LINE_BUFFER_SIZE];
    int  count;
    int  writeIndex;
    int  navIndex; /* -1 = navigation not started */
    SemaphoreHandle_t mutex;
    bool initialized;
} HistoryState;

static HistoryState s_history = {0};

/* Legacy borrowed pointer view for GetAll */
static const char *s_ptrs[DEBUG_CONSOLE_HISTORY_SIZE];

bool HistoryManager_Init(void) {
    if (s_history.initialized) return true;
    memset(&s_history, 0, sizeof(s_history));
    s_history.navIndex = -1;
    s_history.mutex = xSemaphoreCreateMutex();
    if (!s_history.mutex) return false;
    s_history.initialized = true;
    return true;
}

bool HistoryManager_Add(const char *cmd) {
    if (!cmd || cmd[0] == '\0' || !s_history.mutex) {
        return false;
    }

    if (xSemaphoreTake(s_history.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    /* Do not append consecutive duplicate commands */
    if (s_history.count > 0) {
        int lastIdx = (s_history.writeIndex - 1 + DEBUG_CONSOLE_HISTORY_SIZE) %
                      DEBUG_CONSOLE_HISTORY_SIZE;
        if (strncmp(s_history.entries[lastIdx], cmd,
                    DEBUG_CONSOLE_LINE_BUFFER_SIZE - 1) == 0) {
            s_history.navIndex = -1;
            xSemaphoreGive(s_history.mutex);
            return true;
        }
    }

    strncpy(s_history.entries[s_history.writeIndex], cmd,
            DEBUG_CONSOLE_LINE_BUFFER_SIZE - 1);
    s_history.entries[s_history.writeIndex][DEBUG_CONSOLE_LINE_BUFFER_SIZE - 1] = '\0';

    s_history.writeIndex = (s_history.writeIndex + 1) % DEBUG_CONSOLE_HISTORY_SIZE;
    if (s_history.count < DEBUG_CONSOLE_HISTORY_SIZE) {
        s_history.count++;
    }
    s_history.navIndex = -1;

    xSemaphoreGive(s_history.mutex);
    return true;
}

const char *HistoryManager_GetPrevious(void) {
    if (!s_history.mutex) {
        return NULL;
    }

    const char *result = NULL;
    if (xSemaphoreTake(s_history.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return NULL;
    }

    if (s_history.count == 0) {xSemaphoreGive(s_history.mutex);return NULL;}

    if (s_history.navIndex == -1) {
        /* Start navigation at the newest command */
        s_history.navIndex = (s_history.writeIndex - 1 + DEBUG_CONSOLE_HISTORY_SIZE) %
                             DEBUG_CONSOLE_HISTORY_SIZE;
    } else {
        /* Move toward the oldest command */
        int oldestIdx = (s_history.writeIndex - s_history.count +
                         DEBUG_CONSOLE_HISTORY_SIZE) %
                        DEBUG_CONSOLE_HISTORY_SIZE;
        if (s_history.navIndex != oldestIdx) {
            s_history.navIndex = (s_history.navIndex - 1 + DEBUG_CONSOLE_HISTORY_SIZE) %
                                 DEBUG_CONSOLE_HISTORY_SIZE;
        }
    }
    result = s_history.entries[s_history.navIndex];

    xSemaphoreGive(s_history.mutex);
    return result;
}

const char *HistoryManager_GetNext(void) {
    if (!s_history.mutex) {
        return NULL;
    }

    const char *result = NULL;
    if (xSemaphoreTake(s_history.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return NULL;
    }

    int lastAdded = (s_history.writeIndex - 1 + DEBUG_CONSOLE_HISTORY_SIZE) %
                    DEBUG_CONSOLE_HISTORY_SIZE;
    if (s_history.navIndex == -1) {xSemaphoreGive(s_history.mutex);return NULL;}

    if (s_history.navIndex == lastAdded) {
        /* Reset navigation at the newest command */
        s_history.navIndex = -1;
        result = NULL; /* Keep an empty line */
    } else {
        s_history.navIndex = (s_history.navIndex + 1) % DEBUG_CONSOLE_HISTORY_SIZE;
        result = s_history.entries[s_history.navIndex];
    }

    xSemaphoreGive(s_history.mutex);
    return result;
}

void HistoryManager_ResetNavigation(void) {
    if (!s_history.mutex) return;
    if (xSemaphoreTake(s_history.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_history.navIndex = -1;
        xSemaphoreGive(s_history.mutex);
    }
}

const char *const *HistoryManager_GetAll(int *count) {
    if (!s_history.mutex) {
        if (count) *count = 0;
        return s_ptrs;
    }

    if (xSemaphoreTake(s_history.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        if (count) *count = 0;
        return s_ptrs;
    }

    int total = s_history.count;
    int start = (s_history.writeIndex - total + DEBUG_CONSOLE_HISTORY_SIZE) %
                DEBUG_CONSOLE_HISTORY_SIZE;

    for (int i = 0; i < total; i++) {
        s_ptrs[i] = s_history.entries[(start + i) % DEBUG_CONSOLE_HISTORY_SIZE];
    }
    if (count) *count = total;

    xSemaphoreGive(s_history.mutex);
    return s_ptrs;
}

int HistoryManager_GetCount(void) {
    if (!s_history.mutex || xSemaphoreTake(s_history.mutex,pdMS_TO_TICKS(100))!=pdTRUE) return -1;
    int count=s_history.count;xSemaphoreGive(s_history.mutex);return count;
}

void HistoryManager_Clear(void) {
    if (!s_history.mutex) return;
    if (xSemaphoreTake(s_history.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_history.count = 0;
        s_history.writeIndex = 0;
        s_history.navIndex = -1;
        xSemaphoreGive(s_history.mutex);
    }
}

bool HistoryManager_CopyAll(char (*output)[DEBUG_CONSOLE_LINE_BUFFER_SIZE], size_t capacity, int *count) {
    if (!output || !count || capacity<DEBUG_CONSOLE_HISTORY_SIZE || !s_history.mutex ||
        xSemaphoreTake(s_history.mutex,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    int total=s_history.count;
    int start=(s_history.writeIndex-total+DEBUG_CONSOLE_HISTORY_SIZE)%DEBUG_CONSOLE_HISTORY_SIZE;
    for (int i=0;i<total;i++) memcpy(output[i],s_history.entries[(start+i)%DEBUG_CONSOLE_HISTORY_SIZE],DEBUG_CONSOLE_LINE_BUFFER_SIZE);
    *count=total;
    xSemaphoreGive(s_history.mutex);return true;
}
bool HistoryManager_CopyNavigation(bool previous,char *output,size_t capacity) {
    if (!output || capacity<DEBUG_CONSOLE_LINE_BUFFER_SIZE || !s_history.mutex ||
        xSemaphoreTake(s_history.mutex,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    if (!s_history.count || (!previous && s_history.navIndex==-1)) output[0]='\0';
    else {
        int newest=(s_history.writeIndex-1+DEBUG_CONSOLE_HISTORY_SIZE)%DEBUG_CONSOLE_HISTORY_SIZE;
        int oldest=(s_history.writeIndex-s_history.count+DEBUG_CONSOLE_HISTORY_SIZE)%DEBUG_CONSOLE_HISTORY_SIZE;
        if (previous) {
            if (s_history.navIndex==-1) s_history.navIndex=newest;
            else if (s_history.navIndex!=oldest) s_history.navIndex=(s_history.navIndex-1+DEBUG_CONSOLE_HISTORY_SIZE)%DEBUG_CONSOLE_HISTORY_SIZE;
        } else if (s_history.navIndex==newest) s_history.navIndex=-1;
        else s_history.navIndex=(s_history.navIndex+1)%DEBUG_CONSOLE_HISTORY_SIZE;
        if (s_history.navIndex==-1) output[0]='\0';
        else memcpy(output,s_history.entries[s_history.navIndex],DEBUG_CONSOLE_LINE_BUFFER_SIZE);
    }
    xSemaphoreGive(s_history.mutex);return true;
}
