#ifndef IDASHBOARD_H
#define IDASHBOARD_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @file IDashboard.h
 * @brief Dinamik Dashboard arayuzu.
 *
 * Provider kayit/silme, render ve query islemleri.
 */

typedef void (*DashboardValueProvider)(char *buffer, size_t max_len);

typedef struct IDashboard {
    /**
     * @brief Yeni bir gosterge alani kaydeder.
     * @return true basarili, false kapasite dolu
     */
    bool (*RegisterField)(struct IDashboard *self, const char *label,
                          DashboardValueProvider provider);

    /**
     * @brief Bir gosterge alanini kaldirir.
     * @return true basarili, false bulunamadi
     */
    bool (*UnregisterField)(struct IDashboard *self, const char *label);

    /**
     * @brief Dashboard'u render eder (konsola basar).
     */
    void (*Render)(struct IDashboard *self);

    /**
     * @brief Kayitli provider sayisini dondurur.
     */
    int (*GetProviderCount)(struct IDashboard *self);

    /**
     * @brief Belirtilen indeksteki provider etiketini dondurur.
     */
    const char *(*GetProviderLabel)(struct IDashboard *self, int index);

} IDashboard;

/**
 * @brief Dashboard yoneticisi singleton'unu dondurur.
 */
IDashboard *DashboardManager_GetModule(void);

#endif /* IDASHBOARD_H */
