#include "Core/LogManager/LogManagerConfig.h"
#include <stddef.h>

static const LogManagerConfig defaultLogConfig = {.maxLogEntries = 20,
                                                  .vprintfBufferSize = 256,
                                                  .defaultLogLevel =
                                                      ESP_LOG_INFO,
                                                  .enableInterception = false};

const LogManagerConfig *LogManagerConfig_GetDefault(void) {
  return &defaultLogConfig;
}
