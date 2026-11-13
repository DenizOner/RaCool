#ifndef IZIGBEE_ENDPOINT_H
#define IZIGBEE_ENDPOINT_H

#include "esp_zigbee.h"
#include <ezbee/zha.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct IZigbeeEndpoint IZigbeeEndpoint;

struct IZigbeeEndpoint {
  /**
   * @brief Return the human-readable endpoint name.
   */
  const char *(*GetName)(IZigbeeEndpoint *self);

  /**
   * @brief Return the endpoint identifier (1-254).
   */
  uint8_t (*GetEndpointId)(IZigbeeEndpoint *self);

  /**
   * @brief Build and attach ZCL clusters before stack startup.
   * @return ESP_OK only after complete registration. On failure, release
   *         unattached resources; the device owner releases attached endpoints.
   */
  esp_err_t (*RegisterClusters)(IZigbeeEndpoint *self, ezb_af_device_desc_t device_desc);

  /**
   * @brief Configure reporting after network connection.
   */
  esp_err_t (*ConfigureReporting)(IZigbeeEndpoint *self);

  /**
   * @brief Handle an incoming ZCL action.
   * @return true when this endpoint handled the action, false otherwise.
   */
  bool (*HandleAction)(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
                       const void *message, esp_err_t *ret_status);

  /**
   * @brief Perform endpoint-specific work after network connection.
   */
  esp_err_t (*OnConnected)(IZigbeeEndpoint *self);

  /**
   * @brief Handle network disconnection.
   */
  void (*OnDisconnected)(IZigbeeEndpoint *self);

  /**
   * @brief Release endpoint-owned resources or reset static state.
   */
  void (*Destroy)(IZigbeeEndpoint *self);

  /** Linked-list link, immutable while the SDK worker owns the list. */
  IZigbeeEndpoint *next;
};

/**
 * @brief No-op defaults for optional endpoint lifecycle callbacks.
 */
static inline esp_err_t IZigbeeEndpoint_DefaultOnConnected(IZigbeeEndpoint *self) { (void)self; return ESP_OK; }
static inline void IZigbeeEndpoint_DefaultOnDisconnected(IZigbeeEndpoint *self) { (void)self; }
static inline void IZigbeeEndpoint_DefaultDestroy(IZigbeeEndpoint *self) { (void)self; }
static inline bool IZigbeeEndpoint_DefaultHandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id, const void *message, esp_err_t *ret_status) { (void)self; (void)callback_id; (void)message; (void)ret_status; return false; }

#endif // IZIGBEE_ENDPOINT_H
