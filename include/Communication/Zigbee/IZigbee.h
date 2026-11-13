#ifndef IZIGBEE_H
#define IZIGBEE_H

#include "Core/IService.h"
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#include "Communication/Zigbee/IZigbeeEndpoint.h"
#include "Communication/Zigbee/IZigbeeSignalHandler.h"

/**
 * @file IZigbee.h
 * @brief Zigbee service interface definition.
 */

typedef enum {
  ZIGBEE_ERROR_OK = 0,
  ZIGBEE_ERROR_NOT_CONNECTED = -1,
  ZIGBEE_ERROR_STACK_FAIL = -2,
  ZIGBEE_ERROR_TIMEOUT = -3,
  ZIGBEE_ERROR_INVALID_PARAM = -4,
  ZIGBEE_ERROR_LOCK_FAIL = -5,
  ZIGBEE_ERROR_ATTRIBUTE_FAIL = -6,
  ZIGBEE_ERROR_MAX_ENDPOINTS = -7,
  ZIGBEE_ERROR_QUEUE_FULL = -8
} ZigbeeError;

typedef struct {
  uint16_t pan_id;
  uint8_t channel;
  uint16_t short_address;
  uint8_t child_count;
  uint8_t neighbor_count;
  bool is_router;
  int8_t rssi;
  uint8_t lqi;
  bool coordinator_link_valid; /* RSSI/LQI describe a direct coordinator neighbor only. */
  uint32_t uptime_sec;
  uint8_t ext_pan_id[8];
  uint32_t firmware_version;
  uint8_t endpoint_count;
} ZigbeeNetworkInfo;

typedef enum {
  ZIGBEE_POWER_MODE_ACTIVE,
  ZIGBEE_POWER_MODE_LIGHT_SLEEP
} ZigbeePowerMode;

typedef enum {
  ZIGBEE_TX_ECO = 0,
  ZIGBEE_TX_NORMAL = 1,
  ZIGBEE_TX_BOOST = 2
} ZigbeeTxProfile;

typedef struct {
  ZigbeeTxProfile profile;
  int8_t dbm;
} ZigbeeTxStatus;

typedef struct {
  uint32_t operation_id;
  bool pending;
  esp_err_t result;
  bool factory_new;
  bool connected;
} ZigbeeFactoryResetStatus;

typedef struct {
  uint16_t pending;
  uint32_t applied;
  uint32_t failed;
  uint32_t lock_deferrals;
  uint32_t retries;
  uint32_t dropped;
} ZigbeeAttributeStatus;

typedef struct IZigbee {
  IService base;

  /**
   * @brief Notifies Zigbee stack that a ZCL attribute value has changed.
   */
  int32_t (*NotifyAttributeChanged)(struct IZigbee *self, uint8_t endpoint, uint16_t cluster, uint16_t attribute, void *value, uint8_t length);

  /* Copies a local attribute update. Zero means queued and scheduled, not applied.
   * Scheduling failure may retain the update for a later retry. */
  int32_t (*QueueAttributeUpdate)(struct IZigbee *self, uint8_t endpoint, uint16_t cluster, uint16_t attribute, const void *value, uint8_t length);
  int32_t (*GetAttributeStatus)(struct IZigbee *self, ZigbeeAttributeStatus *status);

  /* External task access retains SDK ownership across lock acquisition and use.
   * Zero timeout is nonblocking. Release must run on the acquiring task. */
  bool (*TrySdkAccess)(struct IZigbee *self, uint32_t timeout_ms);
  int32_t (*ReleaseSdkAccess)(struct IZigbee *self);

  int32_t (*ConnectToNetwork)(struct IZigbee *self);
  int32_t (*DisconnectFromNetwork)(struct IZigbee *self);
  int32_t (*IsConnected)(struct IZigbee *self);
  bool (*IsPairing)(struct IZigbee *self);
  int32_t (*ResetToFactorySettings)(struct IZigbee *self);
  /** SDK-owned single-flight reset: a receipt is not completion evidence. */
  int32_t (*BeginFactoryReset)(struct IZigbee *self, uint32_t *operation_id);
  /** Public SDK state plus the latest reset receipt; no private SDK fields. */
  int32_t (*GetFactoryResetStatus)(struct IZigbee *self, ZigbeeFactoryResetStatus *status);
  int32_t (*GetNetworkInfo)(struct IZigbee *self, ZigbeeNetworkInfo *info);
  int32_t (*SetPowerMode)(struct IZigbee *self, ZigbeePowerMode mode);
  /** Synchronous applied SDK + persistence transaction; not an SDK callback API. */
  int32_t (*SetTxProfile)(struct IZigbee *self, ZigbeeTxProfile profile);
  /** Actual SDK configured power; does not measure physical antenna output. */
  int32_t (*GetTxStatus)(struct IZigbee *self, ZigbeeTxStatus *status);
  int32_t (*ManageGroup)(struct IZigbee *self, uint8_t endpoint, uint16_t groupId, bool join);
  int32_t (*RegisterEndpoint)(struct IZigbee *self, IZigbeeEndpoint *endpoint);
  int32_t (*RegisterSignalHandler)(struct IZigbee *self, IZigbeeSignalHandler *handler);

  /**
   * @brief Returns total registered endpoint count.
   */
  uint8_t (*GetEndpointCount)(struct IZigbee *self);

  /**
   * @brief Requests deferred reporting refresh; returns zero if scheduled, not proof of SDK application.
   */
  int32_t (*ReconfigureAllReporting)(struct IZigbee *self);
} IZigbee;

/**
 * @brief Singleton factory for Zigbee module.
 */
IZigbee *Zigbee_Create(void);

#endif // IZIGBEE_H

