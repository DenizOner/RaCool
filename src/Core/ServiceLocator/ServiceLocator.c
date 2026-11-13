#include "Core/ServiceLocator/ServiceLocator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* The registry owns names and arrays, never the registered service objects.
 * Its mutex lives until chip restart so concurrent lookup cannot borrow a
 * deleted mutex. Initialize the registry before starting concurrent consumers. */
typedef struct { char *name; IService *service; } ServiceEntry;
typedef struct { ServiceEntry *services; uint32_t serviceCount, maxServices; } ServiceLocatorImpl;
typedef ServiceLocatorBinding LifecycleEntry;
static SemaphoreHandle_t locatorMutex;
static portMUX_TYPE mutexInstallMux = portMUX_INITIALIZER_UNLOCKED;
static bool isInitialized;
static atomic_bool lifecycleActive;
static ServiceLocatorConfig activeConfig;
static char ownedTag[SERVICE_LOCATOR_TAG_CAPACITY];
static ServiceLocatorImpl locatorInstance;

static bool ensure_mutex(void) {
  portENTER_CRITICAL(&mutexInstallMux);
  SemaphoreHandle_t current = locatorMutex;
  portEXIT_CRITICAL(&mutexInstallMux);
  if (current) return true;
  SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
  if (!candidate) return false;
  portENTER_CRITICAL(&mutexInstallMux);
  if (!locatorMutex) { locatorMutex = candidate; candidate = NULL; }
  portEXIT_CRITICAL(&mutexInstallMux);
  if (candidate) vSemaphoreDelete(candidate);
  return true;
}
static bool take_locator(void) {
  return ensure_mutex() && xSemaphoreTake(locatorMutex,
      pdMS_TO_TICKS(SERVICE_LOCATOR_LOCK_TIMEOUT_MS)) == pdTRUE;
}
static bool valid_name(const char *name) {
  return name && name[0] && strnlen(name, SERVICE_LOCATOR_NAME_CAPACITY) < SERVICE_LOCATOR_NAME_CAPACITY;
}
static int32_t init_locked(const ServiceLocatorConfig *config) {
  if (isInitialized) return 0;
  if (!config) config = ServiceLocatorConfig_GetDefault();
  if (!config || !config->maxServices || config->maxServices > SERVICE_LOCATOR_MAX_SERVICES ||
      !config->locatorTag || !config->locatorTag[0] ||
      strnlen(config->locatorTag, sizeof(ownedTag)) >= sizeof(ownedTag)) return -1;
  ServiceEntry *entries = calloc(config->maxServices, sizeof(*entries));
  if (!entries) return -1;
  memcpy(ownedTag, config->locatorTag, strlen(config->locatorTag) + 1);
  activeConfig = *config;
  activeConfig.locatorTag = ownedTag;
  locatorInstance = (ServiceLocatorImpl){.services = entries, .maxServices = config->maxServices};
  isInitialized = true;
  return 0;
}
static int32_t findServiceIndex(const char *name) {
  for (uint32_t i = 0; i < locatorInstance.serviceCount; ++i)
    if (strcmp(locatorInstance.services[i].name, name) == 0) return (int32_t)i;
  return -1;
}
/* Caller holds the registry mutex; the first binding defines owner order. */
static bool is_first_owner_locked(uint32_t index) {
  for (uint32_t previous=0;previous<index;previous++)
    if (locatorInstance.services[previous].service==locatorInstance.services[index].service) return false;
  return true;
}
static int32_t register_service(const char *name, IService *service) {
  if (!valid_name(name) || !service || !take_locator()) return -1;
  int32_t result = init_locked(NULL);
  if (!result) {
    int32_t index = findServiceIndex(name);
    if (index >= 0) result = locatorInstance.services[index].service == service ? 0 : -3;
    else if (locatorInstance.serviceCount >= locatorInstance.maxServices) result = -4;
    else {
      char *copy = strdup(name);
      if (!copy) result = -1;
      else locatorInstance.services[locatorInstance.serviceCount++] = (ServiceEntry){copy, service};
    }
  }
  xSemaphoreGive(locatorMutex);
  return result;
}
static IService *get_service(const char *name) {
  if (!valid_name(name) || !take_locator()) return NULL;
  IService *service = NULL;
  if (init_locked(NULL) == 0) {
    int32_t index = findServiceIndex(name);
    if (index >= 0) service = locatorInstance.services[index].service;
  }
  xSemaphoreGive(locatorMutex);
  return service;
}
static int32_t unregister_service(const char *name) {
  if (!valid_name(name) || !take_locator()) return -1;
  int32_t result = -1;
  if (isInitialized && !atomic_load(&lifecycleActive)) {
    int32_t index = findServiceIndex(name);
    if (index >= 0) {
      free(locatorInstance.services[index].name);
      --locatorInstance.serviceCount;
      memmove(&locatorInstance.services[index], &locatorInstance.services[index + 1],
          (locatorInstance.serviceCount - (uint32_t)index) * sizeof(ServiceEntry));
      result = 0;
    }
  } else if (atomic_load(&lifecycleActive)) result = -2;
  xSemaphoreGive(locatorMutex);
  return result;
}
/* Callbacks run outside the registry mutex. Removal/deinit and another batch
 * remain rejected until every callback and its owned name snapshot has returned. */
static int32_t run_lifecycle(bool start) {
  if (!take_locator()) return -1;
  if (!isInitialized || atomic_load(&lifecycleActive)) {
    int32_t result = atomic_load(&lifecycleActive) ? -2 : -1;
    xSemaphoreGive(locatorMutex); return result;
  }
  uint32_t count = locatorInstance.serviceCount;
  if (!count) { xSemaphoreGive(locatorMutex); return 0; }
  LifecycleEntry *snapshot = malloc(count * sizeof(*snapshot));
  if (!snapshot) { xSemaphoreGive(locatorMutex); return -1; }
  uint32_t owners=0;
  for (uint32_t i=0;i<count;i++) {
    if (!is_first_owner_locked(i)) continue;
    snapshot[owners].service=locatorInstance.services[i].service;
    strcpy(snapshot[owners].name,locatorInstance.services[i].name);owners++;
  }
  count=owners;
  atomic_store(&lifecycleActive, true);
  xSemaphoreGive(locatorMutex);
  int32_t result = 0;
  for (uint32_t n = 0; n < count; ++n) {
    uint32_t i = start ? n : count - n - 1;
    IService *service = snapshot[i].service;
    int32_t current = 0;
    if (start) current = IService_SafeStart(service);
    else current = service && service->Stop ? service->Stop(service) : -1;
    if (current != 0) {
      if (activeConfig.enableLogging) ESP_LOGE(activeConfig.locatorTag,
          "Service %s failed: %s (result=%ld)", start ? "start" : "stop", snapshot[i].name, (long)current);
      if (!result) result = current;
      if (start) break;
    }
  }
  free(snapshot);
  atomic_store(&lifecycleActive, false);
  return result;
}
static int32_t start_all(void) { return run_lifecycle(true); }
static int32_t stop_all(void) { return run_lifecycle(false); }
static ServiceLocator serviceLocator = {.RegisterService = register_service, .GetService = get_service,
    .UnregisterService = unregister_service, .StartAllServices = start_all, .StopAllServices = stop_all};
int32_t ServiceLocator_Init(const ServiceLocatorConfig *config) {
  if (!take_locator()) return -1;
  int32_t result = init_locked(config);
  xSemaphoreGive(locatorMutex);
  return result;
}
int32_t ServiceLocator_Deinit(void) {
  if (!take_locator()) return -1;
  if (atomic_load(&lifecycleActive)) { xSemaphoreGive(locatorMutex); return -2; }
  for (uint32_t i = 0; i < locatorInstance.serviceCount; ++i) free(locatorInstance.services[i].name);
  free(locatorInstance.services);
  memset(&locatorInstance, 0, sizeof(locatorInstance));
  isInitialized = false;
  xSemaphoreGive(locatorMutex);
  return 0;
}
int32_t ServiceLocator_CopyServices(IService **services, uint32_t capacity, uint32_t *count) {
  if (!count || (!services && capacity) || !take_locator()) return -1;
  int32_t result = init_locked(NULL);
  uint32_t owners=0;
  if (!result) for (uint32_t i=0;i<locatorInstance.serviceCount;i++) if (is_first_owner_locked(i)) owners++;
  if (!result && capacity<owners) result=-1;
  if (!result) {
    uint32_t copied=0;
    for (uint32_t i=0;i<locatorInstance.serviceCount;i++)
      if (is_first_owner_locked(i)) services[copied++]=locatorInstance.services[i].service;
    *count=copied;
  }
  xSemaphoreGive(locatorMutex);
  return result;
}
IService **ServiceLocator_GetAllServices(uint32_t *count) {
  if (!count) return NULL;
  *count = 0;
  if (!take_locator()) return NULL;
  IService **services = NULL;
  if (init_locked(NULL) == 0 && locatorInstance.serviceCount) {
    services = malloc(locatorInstance.serviceCount * sizeof(*services));
    if (services) {
      for (uint32_t i=0;i<locatorInstance.serviceCount;i++)
        if (is_first_owner_locked(i)) services[(*count)++]=locatorInstance.services[i].service;
    }
  }
  xSemaphoreGive(locatorMutex);
  return services;
}
int32_t ServiceLocator_CopyBindings(ServiceLocatorBinding *bindings,uint32_t capacity,uint32_t *count) {
  if (!bindings || !count || !take_locator()) return -1;
  int32_t result=-1;
  if (init_locked(NULL)==0 && capacity>=locatorInstance.serviceCount) {
    for (uint32_t i=0;i<locatorInstance.serviceCount;i++) {
      memset(&bindings[i],0,sizeof(bindings[i]));
      bindings[i].service=locatorInstance.services[i].service;
      strcpy(bindings[i].name,locatorInstance.services[i].name);
    }
    *count=locatorInstance.serviceCount;result=0;
  }
  xSemaphoreGive(locatorMutex);return result;
}
int32_t ServiceLocator_CopyServiceName(IService *service, char *name, size_t capacity) {
  if (!service || !name || !capacity || !take_locator()) return -1;
  int32_t result = -1;
  if (init_locked(NULL) == 0) {
    for (uint32_t i = 0; i < locatorInstance.serviceCount; ++i) {
      if (locatorInstance.services[i].service == service) {
        size_t length = strlen(locatorInstance.services[i].name) + 1;
        if (length <= capacity) { memcpy(name, locatorInstance.services[i].name, length); result = 0; }
        break;
      }
    }
  }
  xSemaphoreGive(locatorMutex);
  return result;
}
int32_t ServiceLocator_RegisterLogManager(ILogManager *logger) { return register_service("LogManager", (IService *)logger); }
ILogManager *ServiceLocator_GetLogManager(void) { return (ILogManager *)get_service("LogManager"); }
IEventBus *ServiceLocator_GetEventBus(void) { return (IEventBus *)get_service("EventBus"); }
int32_t ServiceLocator_RegisterService(const char *name, IService *service) { return register_service(name, service); }
IService *ServiceLocator_GetService(const char *name) { return get_service(name); }
static int32_t registry_bind(IDIRegistry *self, const char *name, IService *service) { (void)self; return register_service(name, service); }
static IService *registry_resolve(IDIRegistry *self, const char *name) { (void)self; return get_service(name); }
static bool registry_has(IDIRegistry *self, const char *name) { (void)self; return get_service(name) != NULL; }
static IDIRegistry diRegistry = {.Bind = registry_bind, .Resolve = registry_resolve, .Has = registry_has};
IDIRegistry *ServiceLocator_GetRegistry(void) { return &diRegistry; }
ServiceLocator *GetServiceLocator(void) { return &serviceLocator; }
