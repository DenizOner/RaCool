#ifndef RACOOL_DEVELOPER_MODE_H
#define RACOOL_DEVELOPER_MODE_H

#include <stdbool.h>

/* Process-local UI state. Reset when the console session starts or stops.
 * This is not an authorization boundary. */
void DeveloperMode_Reset(void);
void DeveloperMode_SetEnabled(bool enabled);
bool DeveloperMode_IsEnabled(void);

/* Command names are classified independently of registration category.
 * Unlisted names fail closed as developer tools. */
bool DeveloperMode_IsUserRoot(const char *name);
bool DeveloperMode_IsVisibleUserRoot(const char *name);
bool DeveloperMode_IsUserLine(const char *line);
const char *DeveloperMode_UserUsage(const char *line);

#endif
