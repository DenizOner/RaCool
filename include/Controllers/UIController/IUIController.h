#ifndef IUICONTROLLER_H
#define IUICONTROLLER_H

#include "Controllers/UIController/UIControllerConfig.h"
#include "Core/IService.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI Event Types for the event-driven architecture.
 */
typedef enum {
  UI_EVENT_TEMP_UPDATE,   /**< Temperature sensor values updated */
  UI_EVENT_ENCODER_MOVE,  /**< KY-040 Encoder rotated */
  UI_EVENT_ENCODER_CLICK, /**< KY-040 Encoder button clicked */
  UI_EVENT_RELAY_STATE,   /**< Relay state change */
  UI_EVENT_ZIGBEE_STATE,  /**< Zigbee connection state change */
  UI_EVENT_SCREEN_WAKEUP, /**< Explicit request to wake up display */
  UI_EVENT_ENCODER_LONG_PRESS, /**< Pairing shortcut; duration in encoder.difference */
  UI_EVENT_ENCODER_PRESS,
  UI_EVENT_ENCODER_HELD,
  UI_EVENT_ENCODER_RELEASE,
  UI_EVENT_CONFIG_CHANGED /**< Refresh settings without treating it as user activity. */
} UIEventType;

/**
 * @brief Structure containing detailed data for UI events.
 */
typedef struct {
  UIEventType type; /**< Type of the event */
  union {
    struct {
      float objectTemp;  /**< Object temperature value */
      float ambientTemp; /**< Ambient temperature value */
      float cpuTemp;     /**< CPU internal temperature value */
    } temp;
    struct {
      int32_t difference; /**< Rotation difference step */
    } encoder;
    struct {
      bool isOpen; /**< State of the relay */
    } relay;
    struct {
      bool isConnected; /**< Zigbee connection status */
    } zigbee;
  } data;
} UIEvent;

typedef struct IUIController IUIController;

/**
 * @brief Interface for UIController service managing display, menu interaction, and LEDs.
 */
struct IUIController {
  IService base;

  /**
   * @brief Initialize the UI controller with a specific configuration.
   * Startup-only: call before concurrent consumers. Reconfiguration after successful
   * initialization is rejected, including while stopped; resources persist across restart.
   * A failed initial allocation preserves the prior configuration and permits retry.
   *
   * @param self Pointer to the IUIController instance.
   * @param config Configuration structure for the UI controller.
   * @param registry Pointer to the dependency injection registry.
   * @return int32_t 0 on success, or a non-zero error code.
   */
  int32_t (*InitializeWithConfig)(IUIController *self,
                                  UIControllerConfig config,
                                  struct IDIRegistry *registry);

  /**
   * @brief Check if the screen is currently on.
   *
   * @param self Pointer to the IUIController instance.
   * @return true if screen is active, false if suspended.
   */
  bool (*IsScreenOn)(IUIController *self);

  /**
   * @brief Post an event to the UIController's event queue.
   *
   * @param self Pointer to the IUIController instance.
   * @param event Pointer to the event payload.
   * @return int32_t 0 on success, or a non-zero error code.
   */
  int32_t (*PostEvent)(IUIController *self, const UIEvent *event);
};

/**
 * @brief Get the singleton instance of the UIController.
 *
 * @return IUIController* Pointer to UIController instance.
 */
IUIController *UIController_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif // IUICONTROLLER_H


