#ifndef SCREEN_BOOT_H
#define SCREEN_BOOT_H

#include <stdint.h>
#include <stdbool.h>

/** Create all LVGL widgets for the Boot animation screen. */
bool ScreenBoot_Create(void);

/** Tick the boot animation. Returns true while still running. */
bool ScreenBoot_Tick(uint32_t nowMs);

/** Returns true while the boot animation is still active. */
bool ScreenBoot_IsRunning(void);

#endif /* SCREEN_BOOT_H */
