#ifndef RACOOL_ZIGBEE_BOOT_HEALTH_H
#define RACOOL_ZIGBEE_BOOT_HEALTH_H

#include "Core/IService.h"

/* Read-only boot readiness from the Zigbee SDK lifecycle owner.
 * Does not require network association, a coordinator or RF delivery.
 * A stale STARTED state without an executing SDK mainloop is never healthy. */
bool Zigbee_IsBootReady(const IService *service);

#endif /* RACOOL_ZIGBEE_BOOT_HEALTH_H */
