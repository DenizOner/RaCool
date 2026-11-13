#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

/**
 * @file DashboardManager.c
 * @brief Dinamik Dashboard yoneticisi.
 *
 * Thread-safe provider kayit/silme, varsayilan sistem metrik provider'lari.
 */

typedef struct {
  char label[32];
  DashboardValueProvider provider;
  bool active;
} DashboardEntry;

static DashboardEntry s_providers[DEBUG_CONSOLE_MAX_PROVIDERS];
static int s_providerCount = 0;
static IDashboard s_dashboardInterface;
static bool s_initialized = false;
static SemaphoreHandle_t s_dashMutex = NULL;

/* ─────────────────────────────────────────────
 * Varsayilan Provider'lar
 * ───────────────────────────────────────────── */

static void provider_uptime(char *buffer, size_t max_len) {
  uint32_t total_s = (uint32_t)(esp_timer_get_time() / 1000000);
  uint32_t days = total_s / 86400;
  uint32_t hours = (total_s % 86400) / 3600;
  uint32_t mins = (total_s % 3600) / 60;
  uint32_t secs = total_s % 60;
  if (days > 0) {
    snprintf(buffer, max_len, "%lug %lus %lud %lus", (unsigned long)days,
             (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
  } else {
    snprintf(buffer, max_len, "%02lu:%02lu:%02lu", (unsigned long)hours,
             (unsigned long)mins, (unsigned long)secs);
  }
}

static void provider_free_heap(char *buffer, size_t max_len) {
  uint32_t free_heap = esp_get_free_heap_size();
  if (free_heap < 10000) {
    snprintf(buffer, max_len, CLI_COLOR_RED "%u byte" CLI_COLOR_RESET,
             (unsigned int)free_heap);
  } else if (free_heap < 30000) {
    snprintf(buffer, max_len, CLI_COLOR_YELLOW "%u byte" CLI_COLOR_RESET,
             (unsigned int)free_heap);
  } else {
    snprintf(buffer, max_len, "%u byte", (unsigned int)free_heap);
  }
}

static void provider_min_heap(char *buffer, size_t max_len) {
  snprintf(buffer, max_len, "%u byte",
           (unsigned int)esp_get_minimum_free_heap_size());
}

/* ─────────────────────────────────────────────
 * Interface Implementasyonu
 * ───────────────────────────────────────────── */

static bool Dashboard_RegisterField(struct IDashboard *self, const char *label,
                                    DashboardValueProvider provider) {
  if (!label || !provider)
    return false;

  if (xSemaphoreTake(s_dashMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  /* Ayni label varsa guncelle */
  for (int i = 0; i < s_providerCount; i++) {
    if (s_providers[i].active && strncmp(s_providers[i].label, label,
                                         sizeof(s_providers[i].label)) == 0) {
      s_providers[i].provider = provider;
      xSemaphoreGive(s_dashMutex);
      return true;
    }
  }

  /* Bos slot bul (silinmis) */
  int slot = -1;
  for (int i = 0; i < s_providerCount; i++) {
    if (!s_providers[i].active) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    if (s_providerCount >= DEBUG_CONSOLE_MAX_PROVIDERS) {
      xSemaphoreGive(s_dashMutex);
      return false;
    }
    slot = s_providerCount++;
  }

  strncpy(s_providers[slot].label, label, sizeof(s_providers[slot].label) - 1);
  s_providers[slot].label[sizeof(s_providers[slot].label) - 1] = '\0';
  s_providers[slot].provider = provider;
  s_providers[slot].active = true;

  xSemaphoreGive(s_dashMutex);
  return true;
}

static bool Dashboard_UnregisterField(struct IDashboard *self,
                                      const char *label) {
  if (!label)
    return false;

  if (xSemaphoreTake(s_dashMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  for (int i = 0; i < s_providerCount; i++) {
    if (s_providers[i].active && strncmp(s_providers[i].label, label,
                                         sizeof(s_providers[i].label)) == 0) {
      s_providers[i].active = false;
      s_providers[i].provider = NULL;
      xSemaphoreGive(s_dashMutex);
      return true;
    }
  }

  xSemaphoreGive(s_dashMutex);
  return false;
}

static void Dashboard_Render(struct IDashboard *self) {
  if (xSemaphoreTake(s_dashMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return;
  }

  printf("\n" CLI_BOLD CLI_COLOR_CYAN
         "+--------------------------------------------------------+\n"
         "|                RACOOL DINAMIK DASHBOARD                |\n"
         "+--------------------------------------------------------+"
         "\n" CLI_COLOR_RESET);

  for (int i = 0; i < s_providerCount; i++) {
    if (!s_providers[i].active || !s_providers[i].provider)
      continue;

    char buffer[64] = "N/A";
    s_providers[i].provider(buffer, sizeof(buffer));
    printf(CLI_COLOR_YELLOW " [%-12s] " CLI_COLOR_RESET "%s\n",
           s_providers[i].label, buffer);
  }

  printf(CLI_COLOR_CYAN "+--------------------------------------------------------+\n" CLI_COLOR_RESET);
  printf(" Cikmak icin herhangi bir tusa basin.\n");

  xSemaphoreGive(s_dashMutex);
}

static int Dashboard_GetProviderCount(struct IDashboard *self) {
  int count = 0;
  if (xSemaphoreTake(s_dashMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int i = 0; i < s_providerCount; i++) {
      if (s_providers[i].active)
        count++;
    }
    xSemaphoreGive(s_dashMutex);
  }
  return count;
}

static const char *Dashboard_GetProviderLabel(struct IDashboard *self,
                                              int index) {
  const char *label = NULL;
  if (xSemaphoreTake(s_dashMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    int active_idx = 0;
    for (int i = 0; i < s_providerCount; i++) {
      if (s_providers[i].active) {
        if (active_idx == index) {
          label = s_providers[i].label;
          break;
        }
        active_idx++;
      }
    }
    xSemaphoreGive(s_dashMutex);
  }
  return label;
}

IDashboard *DashboardManager_GetModule(void) {
  if (!s_initialized) {
    s_dashMutex = xSemaphoreCreateMutex();

    s_dashboardInterface.RegisterField = Dashboard_RegisterField;
    s_dashboardInterface.UnregisterField = Dashboard_UnregisterField;
    s_dashboardInterface.Render = Dashboard_Render;
    s_dashboardInterface.GetProviderCount = Dashboard_GetProviderCount;
    s_dashboardInterface.GetProviderLabel = Dashboard_GetProviderLabel;
    s_initialized = true;

    /* Varsayilan sistem provider'lari */
    Dashboard_RegisterField(&s_dashboardInterface, "UPTIME", provider_uptime);
    Dashboard_RegisterField(&s_dashboardInterface, "FREE HEAP",
                            provider_free_heap);
    Dashboard_RegisterField(&s_dashboardInterface, "MIN HEAP",
                            provider_min_heap);
  }
  return &s_dashboardInterface;
}
