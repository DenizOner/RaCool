#ifndef SCREEN_ZIGBEE_PAIR_H
#define SCREEN_ZIGBEE_PAIR_H

#include "Core/Localization/Localization.h"
#include <stdint.h>
#include <stdbool.h>

/** Create all LVGL widgets for the Zigbee Pairing screen. */
bool ScreenZigbeePair_Create(void);

/**
 * Update Zigbee Pairing screen state machine.
 *
 * Handles four UI states: Pairing (animated search), Connected (show status),
 * Timed-out, and Idle (ready to start).
 */
void ScreenZigbeePair_Update(uint32_t nowMs,
                             bool isZigbeePairing,
                             bool isZigbeeConnected,
                             bool zbActionSelected, const LocalizationCatalog *catalog);

#endif /* SCREEN_ZIGBEE_PAIR_H */
