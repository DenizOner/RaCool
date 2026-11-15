#ifndef OTA_BOOT_RECOVERY_H
#define OTA_BOOT_RECOVERY_H

#include <stdbool.h>

/* Call once after the normal service startup path has finished. Only a new
 * ESP-IDF OTA image in PENDING_VERIFY requires this bounded first-boot gate.
 * This function never modifies NVS, Zigbee pairing or the fan relay. */
void OtaBootRecovery_CompleteFirstBoot(bool startupSucceeded);

#endif /* OTA_BOOT_RECOVERY_H */
