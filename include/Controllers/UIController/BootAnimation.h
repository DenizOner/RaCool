#ifndef BOOT_ANIMATION_H
#define BOOT_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Boot animation states.
 */
typedef enum {
  BOOT_ANIM_STATE_IDLE = 0,
  BOOT_ANIM_STATE_FLASH,      /**< Initial flash screen */
  BOOT_ANIM_STATE_LOGO,       /**< Logo entrance animation */
  BOOT_ANIM_STATE_TAGLINE,    /**< Tagline reveal & device details */
  BOOT_ANIM_STATE_PROGRESS,   /**< Service initialization progress */
  BOOT_ANIM_STATE_DISSOLVE,   /**< Dissolve transition to main screen */
  BOOT_ANIM_STATE_DONE        /**< Animation finished, main screen active */
} BootAnimState;

/**
 * @brief Initialize and start the boot animation sequence.
 */
void BootAnimation_Start(void);

/**
 * @brief Check if boot animation is currently running (including dissolve).
 *
 * @return true if running, false if complete/skipped.
 */
bool BootAnimation_IsRunning(void);

/**
 * @brief Skip the boot animation immediately (e.g. on encoder button click).
 */
void BootAnimation_Skip(void);

/**
 * @brief Publish successful application startup from its owner task.
 * This atomic notification does not certify diagnostics or network delivery.
 */
void BootAnimation_NotifyReady(void);

/**
 * @brief Periodic tick handler called by UI controller.
 *
 * @param nowMs Current system timestamp in milliseconds.
 */
void BootAnimation_Tick(uint32_t nowMs);

#ifdef __cplusplus
}
#endif

#endif // BOOT_ANIMATION_H
