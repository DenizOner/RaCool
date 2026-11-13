#ifndef FLASH_LOGGER_H
#define FLASH_LOGGER_H
#include "Communication/DebugConsole/Core/ILogger.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "Core/LogManager/ILogManager.h"

typedef struct {
    bool active;
    uint32_t admitted, rejected, remaining;
} FlashCaptureStatus;
/* Source remains valid through StopCapture/expiry; no ownership transfer. */
esp_err_t FlashLogger_StartCapture(ILogManager *source, uint32_t seconds, uint32_t records);
esp_err_t FlashLogger_StopCapture(void);
esp_err_t FlashLogger_GetCaptureStatus(FlashCaptureStatus *out);

/* Task-context API. Initialize before concurrent consumers. The filesystem,
 * queue and mutexes remain process-owned across cooperative Stop/Start.
 * Flush waits for accepted writes to close; it is not a power-loss guarantee. */
typedef struct {
    uint32_t totalLogsWritten,droppedLogs,lastLogTimestamp,currentFileIndex,currentFileSize,failedWrites;
} FlashLoggerStats;
ILogger *FlashLogger_GetModule(void);
esp_err_t FlashLogger_Deinit(void);
esp_err_t FlashLogger_Flush(void);
/* Clear serializes with accepted writes; partial filesystem errors are reported. */
esp_err_t FlashLogger_Clear(void);
/* Copy the current file tail into caller-owned output; failures preserve it. */
esp_err_t FlashLogger_CopyTail(char *output,size_t capacity,unsigned maxLines,unsigned *copiedLines);
esp_err_t FlashLogger_AddFilter(const char *tag);
esp_err_t FlashLogger_ClearFilters(void);
bool FlashLogger_IsTagFiltered(const char *tag);
/* Failed getters preserve the caller's output. */
esp_err_t FlashLogger_GetStats(FlashLoggerStats *stats);
esp_err_t FlashLogger_ResetStats(void);
#endif
