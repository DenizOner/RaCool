#ifndef IZIGBEE_SIGNAL_HANDLER_H
#define IZIGBEE_SIGNAL_HANDLER_H

#include "esp_err.h"
#include "esp_zigbee.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef ESP_ZB_SIGNAL_TYPES_DEFINED
#ifndef __ESP_ZIGBEE_TYPE_H__
typedef uint32_t esp_zb_app_signal_type_t;
#endif
#endif

typedef struct IZigbeeSignalHandler IZigbeeSignalHandler;

struct IZigbeeSignalHandler {
  /**
   * @brief Handle Zigbee signals.
   * 
   * @param self Instance
   * @param sig_type Zigbee signal type enum
   * @param err_status Signal error status
   * @param params Signal payload params
   */
  void (*HandleSignal)(IZigbeeSignalHandler *self, esp_zb_app_signal_type_t sig_type, esp_err_t err_status, const void *params);
  IZigbeeSignalHandler *next;
};

#endif // IZIGBEE_SIGNAL_HANDLER_H
