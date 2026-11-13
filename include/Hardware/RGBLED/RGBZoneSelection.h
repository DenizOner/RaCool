#ifndef RGB_ZONE_SELECTION_H
#define RGB_ZONE_SELECTION_H
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include <stdbool.h>
#include <stdint.h>
#include "Management/ConfigurationManager/SystemConfigs.h"
/* One menu-owner draft; atomic publication allows concurrent frame snapshots. */
int RGBZoneSelection_Zone(UIState state);
void RGBZoneSelection_Begin(UIState state, uint16_t x, uint16_t y);
bool RGBZoneSelection_Read(UIState state, uint16_t *x, uint16_t *y);
bool RGBZoneSelection_Rotate(UIState state, int direction, uint16_t *x, uint16_t *y);
void RGBZoneSelection_Clear(void);
#endif
