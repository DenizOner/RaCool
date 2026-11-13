#include "Hardware/ResourceRegistry/IResourceRegistry.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "esp_log.h"
#include "Core/Localization/Localization.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @file ResourceRegistry.c
 * @brief Bounded hardware reservation ownership and statistics.
 */

static const char *TAG = "ResourceRegistry";
#define MAX_OWNER_LEN RESOURCE_REGISTRY_OWNER_CAPACITY

/**
 * @brief Representation of an individual resource entry in memory.
 */
typedef struct {
    char owner[MAX_OWNER_LEN];  /**< Current owner module name. */
    bool shared;                /**< True if resource is reserved in shared mode. */
    bool is_reserved;           /**< True if resource is currently active/reserved. */
    uint32_t ref_count;         /**< Shared reservation counter. */
    char owners[RESOURCE_REGISTRY_MAX_SHARED_OWNERS][MAX_OWNER_LEN];
    uint32_t owner_refs[RESOURCE_REGISTRY_MAX_SHARED_OWNERS];
} ResourceEntry_t;

/**
 * @brief Internal concrete state structure of ResourceRegistry.
 */
typedef struct {
    IResourceRegistry base;
    ServiceState state;
    ResourceRegistryConfig config;
    SemaphoreHandle_t mutex;
    IEventBus *eventBus;

    ResourceEntry_t *gpio_map;
    ResourceEntry_t *i2c_map;
    ResourceEntry_t *pwm_map;
    ResourceEntry_t *adc_map;

    ResourceRegistryStats_t stats;
} ResourceRegistryImpl;

static ResourceRegistryImpl res_reg_inst;
static portMUX_TYPE registryInstallMux = portMUX_INITIALIZER_UNLOCKED;

static bool TakeRegistry(void) {
    portENTER_CRITICAL(&registryInstallMux);
    SemaphoreHandle_t current = res_reg_inst.mutex;
    portEXIT_CRITICAL(&registryInstallMux);
    if (!current) {
        SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
        if (!candidate) return false;
        portENTER_CRITICAL(&registryInstallMux);
        if (!res_reg_inst.mutex) { res_reg_inst.mutex = candidate; candidate = NULL; }
        portEXIT_CRITICAL(&registryInstallMux);
        if (candidate) vSemaphoreDelete(candidate);
    }
    TickType_t ticks = pdMS_TO_TICKS(RESOURCE_REGISTRY_LOCK_TIMEOUT_MS);
    if (ticks == 0) ticks = 1;
    return xSemaphoreTake(res_reg_inst.mutex, ticks) == pdTRUE;
}

static bool ValidOwner(const char *owner) {
    return owner && *owner && strnlen(owner, MAX_OWNER_LEN) < MAX_OWNER_LEN;
}

/* Call only while owning the registry mutex, including initialization/reset. */
static bool HasReservations(void) {
    ResourceEntry_t *maps[] = {res_reg_inst.gpio_map, res_reg_inst.i2c_map,
                              res_reg_inst.pwm_map, res_reg_inst.adc_map};
    uint32_t sizes[] = {res_reg_inst.config.max_gpio_entries, res_reg_inst.config.max_i2c_entries,
                        res_reg_inst.config.max_pwm_entries, res_reg_inst.config.max_adc_entries};
    for (unsigned t = 0; t < 4; ++t)
        for (uint32_t i = 0; maps[t] && i < sizes[t]; ++i)
            if (maps[t][i].is_reserved) return true;
    return false;
}

/**
 * @brief Helper to publish resource events onto EventBus safely.
 */
static void PublishResourceEvent(const char *eventName, ResourceId_t resource, const char *owner, bool shared, int32_t res_code) {
    if (!TakeRegistry()) return;
    IEventBus *bus = res_reg_inst.eventBus;
    bool enabled = res_reg_inst.config.enable_events;
    xSemaphoreGive(res_reg_inst.mutex);
    if (!enabled || !bus || !bus->Publish) return;

    ResourceRegistryEvent_t evtData;
    memset(&evtData, 0, sizeof(evtData));
    evtData.resource = resource;
    evtData.shared = shared;
    evtData.result_code = res_code;
    if (owner) {
        strncpy(evtData.owner, owner, sizeof(evtData.owner) - 1);
    }

    BusEvent event = {
        .eventName = eventName,
        .type = (res_code == 0) ? EVENT_TYPE_INFO : EVENT_TYPE_WARNING,
        .data = &evtData,
        .dataSize = sizeof(evtData)
    };

    bus->Publish(event);
}

/**
 * @brief Helper to resolve map pointer and capacity for a given resource.
 */
static ResourceEntry_t *GetResourceEntry(ResourceId_t resource, uint32_t *out_max) {
    switch (resource.type) {
        case RESOURCE_TYPE_GPIO:
            if (out_max) *out_max = res_reg_inst.config.max_gpio_entries;
            return res_reg_inst.gpio_map;
        case RESOURCE_TYPE_I2C_BUS:
            if (out_max) *out_max = res_reg_inst.config.max_i2c_entries;
            return res_reg_inst.i2c_map;
        case RESOURCE_TYPE_PWM_CHANNEL:
            if (out_max) *out_max = res_reg_inst.config.max_pwm_entries;
            return res_reg_inst.pwm_map;
        case RESOURCE_TYPE_ADC_CHANNEL:
            if (out_max) *out_max = res_reg_inst.config.max_adc_entries;
            return res_reg_inst.adc_map;
        default:
            if (out_max) *out_max = 0;
            return NULL;
    }
}

static int32_t ResourceRegistry_Reserve(IResourceRegistry *self,
                                         ResourceId_t resource,
                                         const char *owner, bool shared) {
    if (!ValidOwner(owner)) {
        return -1;
    }

    if (!TakeRegistry()) return -4;
    if (res_reg_inst.state != SERVICE_STATE_INITIALIZED && res_reg_inst.state != SERVICE_STATE_STARTED) {
        xSemaphoreGive(res_reg_inst.mutex); return -4;
    }
    uint32_t max_entries = 0;
    ResourceEntry_t *map = GetResourceEntry(resource, &max_entries);
    if (!map || resource.id >= max_entries) {
        xSemaphoreGive(res_reg_inst.mutex); return -1;
    }
    ResourceEntry_t *entry = &map[resource.id];

    if (entry->is_reserved) {
        if (entry->shared && shared) {
            unsigned slot = RESOURCE_REGISTRY_MAX_SHARED_OWNERS;
            for (unsigned i = 0; i < RESOURCE_REGISTRY_MAX_SHARED_OWNERS; ++i)
                if (entry->owner_refs[i] && strcmp(entry->owners[i], owner) == 0) { slot = i; break; }
            if (slot == RESOURCE_REGISTRY_MAX_SHARED_OWNERS)
                for (unsigned i = 0; i < RESOURCE_REGISTRY_MAX_SHARED_OWNERS; ++i)
                    if (!entry->owner_refs[i]) { slot = i; break; }
            if (slot == RESOURCE_REGISTRY_MAX_SHARED_OWNERS || entry->ref_count == UINT32_MAX ||
                entry->owner_refs[slot] == UINT32_MAX) {
                xSemaphoreGive(res_reg_inst.mutex); return -4;
            }
            memcpy(entry->owners[slot], owner, strlen(owner) + 1);
            entry->owner_refs[slot]++;
            entry->ref_count++;
            if (res_reg_inst.config.enable_stats) {
                res_reg_inst.stats.total_reservations++;
            }
            if (res_reg_inst.config.enable_logging) {
                ESP_LOGI(TAG, "Resource %lu type %d shared by %s (ref_count: %lu)",
                         (unsigned long)resource.id, (int)resource.type, owner, (unsigned long)entry->ref_count);
            }
            xSemaphoreGive(res_reg_inst.mutex);
            PublishResourceEvent(RESOURCE_EVENT_RESERVED, resource, owner, shared, 0);
            return 0;
        }

        if (res_reg_inst.config.enable_stats) {
            res_reg_inst.stats.total_conflicts++;
        }
        ESP_LOGE(TAG, "CONFLICT: Resource %lu type %d requested by %s but locked by %s",
                 (unsigned long)resource.id, (int)resource.type, owner, entry->owner);
        xSemaphoreGive(res_reg_inst.mutex);
        PublishResourceEvent(RESOURCE_EVENT_CONFLICT, resource, owner, shared, -3);
        return -3; // Conflict
    }

    entry->is_reserved = true;
    entry->shared = shared;
    entry->ref_count = 1;
    memcpy(entry->owners[0], owner, strlen(owner) + 1);
    entry->owner_refs[0] = 1;
    strncpy(entry->owner, owner, MAX_OWNER_LEN - 1);
    entry->owner[MAX_OWNER_LEN - 1] = '\0';

    if (res_reg_inst.config.enable_stats) {
        res_reg_inst.stats.total_reservations++;
        if (resource.type == RESOURCE_TYPE_GPIO) res_reg_inst.stats.active_gpio_count++;
        else if (resource.type == RESOURCE_TYPE_I2C_BUS) res_reg_inst.stats.active_i2c_count++;
        else if (resource.type == RESOURCE_TYPE_PWM_CHANNEL) res_reg_inst.stats.active_pwm_count++;
        else if (resource.type == RESOURCE_TYPE_ADC_CHANNEL) res_reg_inst.stats.active_adc_count++;
    }

    if (res_reg_inst.config.enable_logging) {
        ESP_LOGI(TAG, "Resource %lu type %d reserved by %s (shared: %d)",
                 (unsigned long)resource.id, (int)resource.type, owner, shared);
    }

    xSemaphoreGive(res_reg_inst.mutex);
    PublishResourceEvent(RESOURCE_EVENT_RESERVED, resource, owner, shared, 0);
    return 0;
}

static bool ResourceRegistry_IsAvailable(IResourceRegistry *self,
                                          ResourceId_t resource) {
    if (!TakeRegistry()) return false;
    uint32_t max_entries = 0;
    ResourceEntry_t *map = GetResourceEntry(resource, &max_entries);
    if (!map || resource.id >= max_entries) {
        xSemaphoreGive(res_reg_inst.mutex); return false;
    }
    bool available = !map[resource.id].is_reserved || map[resource.id].shared;
    xSemaphoreGive(res_reg_inst.mutex);

    return available;
}

static int32_t ResourceRegistry_Release(IResourceRegistry *self,
                                         ResourceId_t resource,
                                         const char *owner) {
    if (!ValidOwner(owner)) return -1;
    if (!TakeRegistry()) return -4;

    uint32_t max_entries = 0;
    ResourceEntry_t *map = GetResourceEntry(resource, &max_entries);
    if (!map || resource.id >= max_entries) {
        xSemaphoreGive(res_reg_inst.mutex); return -1;
    }
    ResourceEntry_t *entry = &map[resource.id];

    if (!entry->is_reserved) {
        xSemaphoreGive(res_reg_inst.mutex);
        return -2; // Not reserved
    }

    unsigned slot = RESOURCE_REGISTRY_MAX_SHARED_OWNERS;
    for (unsigned i = 0; i < RESOURCE_REGISTRY_MAX_SHARED_OWNERS; ++i)
        if (entry->owner_refs[i] && strcmp(entry->owners[i], owner) == 0) { slot = i; break; }
    if (slot == RESOURCE_REGISTRY_MAX_SHARED_OWNERS) {
        ESP_LOGW(TAG, "Release DENIED: Resource %lu type %d owned by %s, release attempted by %s",
                 (unsigned long)resource.id, (int)resource.type, entry->owner, owner);
        xSemaphoreGive(res_reg_inst.mutex);
        return -3; // Owner mismatch
    }

    if (entry->shared && entry->ref_count > 1) {
        entry->owner_refs[slot]--;
        if (!entry->owner_refs[slot]) entry->owners[slot][0] = '\0';
        entry->ref_count--;
        for (unsigned i = 0; i < RESOURCE_REGISTRY_MAX_SHARED_OWNERS; ++i)
            if (entry->owner_refs[i]) { memcpy(entry->owner, entry->owners[i], MAX_OWNER_LEN); break; }
        if (res_reg_inst.config.enable_stats) {
            res_reg_inst.stats.total_releases++;
        }
        if (res_reg_inst.config.enable_logging) {
            ESP_LOGI(TAG, "Resource %lu type %d shared release by %s (remaining ref_count: %lu)",
                     (unsigned long)resource.id, (int)resource.type, owner, (unsigned long)entry->ref_count);
        }
        bool was_shared = entry->shared;
        xSemaphoreGive(res_reg_inst.mutex);
        PublishResourceEvent(RESOURCE_EVENT_RELEASED, resource, owner, was_shared, 0);
        return 0;
    }

    bool was_shared = entry->shared;
    memset(entry, 0, sizeof(*entry));

    if (res_reg_inst.config.enable_stats) {
        res_reg_inst.stats.total_releases++;
        if (resource.type == RESOURCE_TYPE_GPIO && res_reg_inst.stats.active_gpio_count > 0) res_reg_inst.stats.active_gpio_count--;
        else if (resource.type == RESOURCE_TYPE_I2C_BUS && res_reg_inst.stats.active_i2c_count > 0) res_reg_inst.stats.active_i2c_count--;
        else if (resource.type == RESOURCE_TYPE_PWM_CHANNEL && res_reg_inst.stats.active_pwm_count > 0) res_reg_inst.stats.active_pwm_count--;
        else if (resource.type == RESOURCE_TYPE_ADC_CHANNEL && res_reg_inst.stats.active_adc_count > 0) res_reg_inst.stats.active_adc_count--;
    }

    if (res_reg_inst.config.enable_logging) {
        ESP_LOGI(TAG, "Resource %lu type %d released by %s", (unsigned long)resource.id, (int)resource.type, owner);
    }

    xSemaphoreGive(res_reg_inst.mutex);
    PublishResourceEvent(RESOURCE_EVENT_RELEASED, resource, owner, was_shared, 0);
    return 0;
}

static bool ResourceRegistry_Validate(IResourceRegistry *self) {
    bool valid = true;
    if (!TakeRegistry()) return false;

    // Cross check for any invalid state or out of bounds allocations
    if (res_reg_inst.state != SERVICE_STATE_INITIALIZED && res_reg_inst.state != SERVICE_STATE_STARTED) {
        valid = false;
    }
    ResourceEntry_t *maps[] = {res_reg_inst.gpio_map, res_reg_inst.i2c_map, res_reg_inst.pwm_map, res_reg_inst.adc_map};
    uint32_t sizes[] = {res_reg_inst.config.max_gpio_entries, res_reg_inst.config.max_i2c_entries,
                        res_reg_inst.config.max_pwm_entries, res_reg_inst.config.max_adc_entries};
    for (unsigned t = 0; valid && t < 4; ++t) {
        if (!maps[t] || !sizes[t]) { valid = false; break; }
        for (uint32_t i = 0; valid && i < sizes[t]; ++i) {
            ResourceEntry_t *entry = &maps[t][i];
            uint64_t refs = 0;
            unsigned owners = 0;
            bool primary_found = false;
            for (unsigned k = 0; k < RESOURCE_REGISTRY_MAX_SHARED_OWNERS; ++k) {
                if (!entry->owner_refs[k]) continue;
                owners++;
                refs += entry->owner_refs[k];
                if (!ValidOwner(entry->owners[k])) { valid = false; break; }
                if (memcmp(entry->owner, entry->owners[k], MAX_OWNER_LEN) == 0) primary_found = true;
                for (unsigned j = 0; j < k; ++j)
                    if (entry->owner_refs[j] && strcmp(entry->owners[k], entry->owners[j]) == 0) valid = false;
            }
            if (entry->is_reserved) {
                if (!owners || !refs || refs != entry->ref_count || !primary_found ||
                    (!entry->shared && (owners != 1 || refs != 1))) valid = false;
            } else if (owners || refs || entry->ref_count || entry->shared) valid = false;
        }
    }

    xSemaphoreGive(res_reg_inst.mutex);
    return valid;
}

static int32_t ResourceRegistry_GetOwner(IResourceRegistry *self, ResourceId_t resource, char *out_owner, size_t max_len) {
    if (!out_owner || max_len == 0) return -1;
    if (!TakeRegistry()) return -4;
    uint32_t max_entries = 0;
    ResourceEntry_t *map = GetResourceEntry(resource, &max_entries);
    if (!map || resource.id >= max_entries) { xSemaphoreGive(res_reg_inst.mutex); return -1; }
    if (!map[resource.id].is_reserved) {
        xSemaphoreGive(res_reg_inst.mutex);
        return -2;
    }
    size_t needed = strlen(map[resource.id].owner) + 1;
    if (needed > max_len) { xSemaphoreGive(res_reg_inst.mutex); return -1; }
    memcpy(out_owner, map[resource.id].owner, needed);
    xSemaphoreGive(res_reg_inst.mutex);
    return 0;
}

static const char *res_type_str(ResourceType_t type) {
    switch (type) {
        case RESOURCE_TYPE_GPIO: return "GPIO";
        case RESOURCE_TYPE_I2C_BUS: return "I2C Bus";
        case RESOURCE_TYPE_PWM_CHANNEL: return "PWM Ch";
        case RESOURCE_TYPE_ADC_CHANNEL: return "ADC Ch";
        default: return "Unknown";
    }
}

static int32_t ResourceRegistry_DumpStatus(IResourceRegistry *self) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    typedef struct { ResourceId_t resource; char owner[MAX_OWNER_LEN]; bool shared; uint32_t refs; } DumpRow;
    if (!TakeRegistry()) return -4;
    uint32_t capacity = res_reg_inst.config.max_gpio_entries + res_reg_inst.config.max_i2c_entries +
                        res_reg_inst.config.max_pwm_entries + res_reg_inst.config.max_adc_entries;
    DumpRow *rows = capacity ? calloc(capacity, sizeof(*rows)) : NULL;
    if (capacity && !rows) { xSemaphoreGive(res_reg_inst.mutex); return -1; }
    uint32_t count = 0;
    ResourceType_t types[] = {RESOURCE_TYPE_GPIO, RESOURCE_TYPE_I2C_BUS, RESOURCE_TYPE_PWM_CHANNEL, RESOURCE_TYPE_ADC_CHANNEL};
    for (unsigned t = 0; t < 4; ++t) {
        uint32_t maximum = 0;
        ResourceEntry_t *map = GetResourceEntry((ResourceId_t){types[t], 0}, &maximum);
        for (uint32_t i = 0; map && i < maximum; ++i) {
            if (!map[i].is_reserved) continue;
            rows[count].resource = (ResourceId_t){types[t], i};
            memcpy(rows[count].owner, map[i].owner, MAX_OWNER_LEN);
            rows[count].shared = map[i].shared;
            rows[count++].refs = map[i].ref_count;
        }
    }
    xSemaphoreGive(res_reg_inst.mutex);

    printf("\n" CLI_COLOR_CYAN "+---------------------+---------+-------------------+---------+-----------+" CLI_COLOR_RESET "\n");
    LOC_PRINTF(catalog,LOC_CLI_RESOURCE_DUMP_HEADER,"| Resource Type       | ID      | Owner             | Shared  | RefCount  |\n");
    printf(CLI_COLOR_CYAN "+---------------------+---------+-------------------+---------+-----------+" CLI_COLOR_RESET "\n");

    for (uint32_t i = 0; i < count; ++i) {
                printf(CLI_COLOR_CYAN "|" CLI_COLOR_RESET " %-19s " CLI_COLOR_CYAN "|" CLI_COLOR_RESET " %-7lu " CLI_COLOR_CYAN "|" CLI_COLOR_RESET " %-17s " CLI_COLOR_CYAN "|" CLI_COLOR_RESET " %-7s " CLI_COLOR_CYAN "|" CLI_COLOR_RESET " %-9lu " CLI_COLOR_CYAN "|" CLI_COLOR_RESET "\n",
                       res_type_str(rows[i].resource.type), (unsigned long)rows[i].resource.id, rows[i].owner,
                       Localization_Text(catalog,rows[i].shared ? LOC_COMMON_YES : LOC_COMMON_NO),
                       (unsigned long)rows[i].refs);
    }

    printf(CLI_COLOR_CYAN "+---------------------+---------+-------------------+---------+-----------+" CLI_COLOR_RESET "\n\n");

    free(rows);
    return 0;
}

static int32_t ResourceRegistry_GetStats(IResourceRegistry *self, ResourceRegistryStats_t *out_stats) {
    if (!out_stats) return -1;
    if (!TakeRegistry()) return -4;
    *out_stats = res_reg_inst.stats;
    uint32_t active[4] = {0};
    ResourceEntry_t *maps[] = {res_reg_inst.gpio_map, res_reg_inst.i2c_map, res_reg_inst.pwm_map, res_reg_inst.adc_map};
    uint32_t sizes[] = {res_reg_inst.config.max_gpio_entries, res_reg_inst.config.max_i2c_entries,
                        res_reg_inst.config.max_pwm_entries, res_reg_inst.config.max_adc_entries};
    for (unsigned t = 0; t < 4; ++t)
        for (uint32_t i = 0; maps[t] && i < sizes[t]; ++i)
            if (maps[t][i].is_reserved) active[t]++;
    out_stats->active_gpio_count = active[0]; out_stats->active_i2c_count = active[1];
    out_stats->active_pwm_count = active[2]; out_stats->active_adc_count = active[3];
    xSemaphoreGive(res_reg_inst.mutex);
    return 0;
}

static int32_t ResourceRegistry_Reset(IResourceRegistry *self) {
    if (!TakeRegistry()) return -4;
    if (HasReservations()) { xSemaphoreGive(res_reg_inst.mutex); return -4; }

    if (res_reg_inst.gpio_map) memset(res_reg_inst.gpio_map, 0, res_reg_inst.config.max_gpio_entries * sizeof(ResourceEntry_t));
    if (res_reg_inst.i2c_map) memset(res_reg_inst.i2c_map, 0, res_reg_inst.config.max_i2c_entries * sizeof(ResourceEntry_t));
    if (res_reg_inst.pwm_map) memset(res_reg_inst.pwm_map, 0, res_reg_inst.config.max_pwm_entries * sizeof(ResourceEntry_t));
    if (res_reg_inst.adc_map) memset(res_reg_inst.adc_map, 0, res_reg_inst.config.max_adc_entries * sizeof(ResourceEntry_t));

    memset(&res_reg_inst.stats, 0, sizeof(res_reg_inst.stats));

    xSemaphoreGive(res_reg_inst.mutex);
    ESP_LOGI(TAG, "ResourceRegistry maps and statistics reset");
    return 0;
}

static int32_t ResourceRegistry_InitializeWithConfig(IResourceRegistry *self,
                                                      ResourceRegistryConfig config,
                                                      struct IDIRegistry *registry) {
    ESP_LOGI(TAG, "Initializing Resource Registry with config");

    if (!config.max_gpio_entries || config.max_gpio_entries > RESOURCE_REGISTRY_MAX_GPIO_ENTRIES ||
        !config.max_i2c_entries || config.max_i2c_entries > RESOURCE_REGISTRY_MAX_I2C_ENTRIES ||
        !config.max_pwm_entries || config.max_pwm_entries > RESOURCE_REGISTRY_MAX_PWM_ENTRIES ||
        !config.max_adc_entries || config.max_adc_entries > RESOURCE_REGISTRY_MAX_ADC_ENTRIES) return -1;
    IEventBus *bus = NULL;
    if (registry) {
        if (!registry->Resolve) return -1;
        bus = (IEventBus *)registry->Resolve(registry, SVC_NAME_EVENT_BUS);
    }
    if (!TakeRegistry()) return -4;
    if (HasReservations()) { xSemaphoreGive(res_reg_inst.mutex); return -4; }
    ResourceEntry_t *gpio = calloc(config.max_gpio_entries, sizeof(*gpio));
    ResourceEntry_t *i2c = calloc(config.max_i2c_entries, sizeof(*i2c));
    ResourceEntry_t *pwm = calloc(config.max_pwm_entries, sizeof(*pwm));
    ResourceEntry_t *adc = calloc(config.max_adc_entries, sizeof(*adc));
    if (!gpio || !i2c || !pwm || !adc) {
        free(gpio); free(i2c); free(pwm); free(adc);
        xSemaphoreGive(res_reg_inst.mutex); return -1;
    }
    free(res_reg_inst.gpio_map); free(res_reg_inst.i2c_map);
    free(res_reg_inst.pwm_map); free(res_reg_inst.adc_map);
    res_reg_inst.gpio_map = gpio; res_reg_inst.i2c_map = i2c;
    res_reg_inst.pwm_map = pwm; res_reg_inst.adc_map = adc;
    res_reg_inst.config = config;
    res_reg_inst.eventBus = bus;

    memset(&res_reg_inst.stats, 0, sizeof(res_reg_inst.stats));
    res_reg_inst.state = SERVICE_STATE_INITIALIZED;
    xSemaphoreGive(res_reg_inst.mutex);

    return 0;
}

static int32_t ResourceRegistry_Initialize(struct IService *self,
                                           struct IDIRegistry *registry) {
    return ResourceRegistry_InitializeWithConfig(
        (IResourceRegistry *)self, ResourceRegistryConfig_GetDefault(),
        registry);
}

static int32_t ResourceRegistry_Start(struct IService *self) {
    if (!TakeRegistry()) return -4;
    if (res_reg_inst.state != SERVICE_STATE_INITIALIZED && res_reg_inst.state != SERVICE_STATE_STARTED &&
        res_reg_inst.state != SERVICE_STATE_STOPPED) { xSemaphoreGive(res_reg_inst.mutex); return -1; }
    res_reg_inst.state = SERVICE_STATE_STARTED;
    xSemaphoreGive(res_reg_inst.mutex);
    return 0;
}

static int32_t ResourceRegistry_Stop(struct IService *self) {
    if (!TakeRegistry()) return -4;
    res_reg_inst.state = SERVICE_STATE_STOPPED;
    xSemaphoreGive(res_reg_inst.mutex);
    return 0;
}

static ServiceState ResourceRegistry_GetState(struct IService *self) {
    if (!TakeRegistry()) return SERVICE_STATE_ERROR;
    ServiceState st = res_reg_inst.state;
    xSemaphoreGive(res_reg_inst.mutex);
    return st;
}

static int32_t ResourceRegistry_CheckHealth(struct IService *self) {
    if (!TakeRegistry()) return -4;
    int32_t res = (res_reg_inst.state == SERVICE_STATE_STARTED) ? 0 : -1;
    xSemaphoreGive(res_reg_inst.mutex);
    return res;
}

IResourceRegistry *ResourceRegistry_GetInstance(void) {
    static bool initialized = false;
    portENTER_CRITICAL(&registryInstallMux);
    if (!initialized) {
        initialized = true;

        res_reg_inst.base.base.Initialize = ResourceRegistry_Initialize;
        res_reg_inst.base.base.Start = ResourceRegistry_Start;
        res_reg_inst.base.base.Stop = ResourceRegistry_Stop;
        res_reg_inst.base.base.GetState = ResourceRegistry_GetState;
        res_reg_inst.base.base.CheckHealth = ResourceRegistry_CheckHealth;

        res_reg_inst.base.InitializeWithConfig = ResourceRegistry_InitializeWithConfig;
        res_reg_inst.base.Reserve = ResourceRegistry_Reserve;
        res_reg_inst.base.IsAvailable = ResourceRegistry_IsAvailable;
        res_reg_inst.base.Release = ResourceRegistry_Release;
        res_reg_inst.base.Validate = ResourceRegistry_Validate;
        res_reg_inst.base.GetOwner = ResourceRegistry_GetOwner;
        res_reg_inst.base.DumpStatus = ResourceRegistry_DumpStatus;
        res_reg_inst.base.GetStats = ResourceRegistry_GetStats;
        res_reg_inst.base.Reset = ResourceRegistry_Reset;

        res_reg_inst.state = SERVICE_STATE_UNINITIALIZED;
        res_reg_inst.eventBus = NULL;
        res_reg_inst.gpio_map = NULL;
        res_reg_inst.i2c_map = NULL;
        res_reg_inst.pwm_map = NULL;
        res_reg_inst.adc_map = NULL;
    }
    portEXIT_CRITICAL(&registryInstallMux);
    return &res_reg_inst.base;
}
