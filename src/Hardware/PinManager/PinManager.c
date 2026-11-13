#include "Core/Localization/Localization.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/PinManager/PinDefinitions.h"
#include "Hardware/PinManager/PinManagerConfig.h"
#include "Hardware/PinManager/PinEvents.h"
#include "Hardware/ResourceRegistry/IResourceRegistry.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

/**
 * @file PinManager.c
 * @brief PinManager service implementation.
 *
 * Centrally manages the system GPIO pins.
 * Provides thread safety, ISR dispatch and debouncing.
 */

static const char *TAG = "PinManager";

/**
 * @brief Internal state for each pin function.
 */
typedef struct {
    gpio_num_t gpio;                ///< Assigned GPIO number
    PinInterruptHandler_t handler;  ///< Registered ISR handler
    void *handlerArg;               ///< Handler argument
    uint32_t debounceMs;            ///< Debounce interval (ms)
    uint64_t lastInterruptTime;     ///< Last interrupt timestamp (us)
    PinInterruptType_t intrType;    ///< Interrupt type
    bool reserved;                 ///< Registry ownership retained across repeated validation
    gpio_num_t reservedGPIO;        ///< GPIO associated with the retained reservation
    bool interruptAttached, dispatchEnabled, cleanupPending, hasInterruptTime;
    uint32_t inFlight;             ///< Captured ISR callback/publisher ownership
} PinInternalInfo_t;

/**
 * @brief Internal PinManager service state.
 */
typedef struct {
  IPinManager base;
  _Atomic(ServiceState) state;
  PinInternalInfo_t pinMap[PIN_FUNC_MAX_COUNT];
  IResourceRegistry *resourceRegistry;
  IEventBus *eventBus;
  SemaphoreHandle_t mutex;
} PinManagerServiceImpl;

static PinManagerServiceImpl pinManagerInstance;
static portMUX_TYPE pinISRLock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool pinInitializing;
static atomic_bool pinConfigurationApplied;
static PinManagerConfig appliedPinConfig;
static bool hasAppliedPinConfig;
static bool PinManager_SameConfig(const PinManagerConfig *a, const PinManagerConfig *b) {
    if (a->defaultDebounceMs != b->defaultDebounceMs) return false;
    for (unsigned i = 0; i < PIN_FUNC_MAX_COUNT; ++i) {
        if (a->entries[i].pin != b->entries[i].pin || a->entries[i].mode != b->entries[i].mode ||
            a->entries[i].pull != b->entries[i].pull || a->entries[i].initialState != b->entries[i].initialState ||
            a->entries[i].debounceMs != b->entries[i].debounceMs) return false;
    }
    return true;
}
static portMUX_TYPE pinInstallMux = portMUX_INITIALIZER_UNLOCKED;
static bool PinManager_EnsureMutex(void) {
    portENTER_CRITICAL(&pinInstallMux);
    SemaphoreHandle_t current = pinManagerInstance.mutex;
    portEXIT_CRITICAL(&pinInstallMux);
    if (current) return true;
    SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
    if (!candidate) return false;
    portENTER_CRITICAL(&pinInstallMux);
    if (!pinManagerInstance.mutex) { pinManagerInstance.mutex = candidate; candidate = NULL; }
    portEXIT_CRITICAL(&pinInstallMux);
    if (candidate) vSemaphoreDelete(candidate);
    return true;
}
static int32_t PinManager_FinishInitialize(int32_t result) {
    atomic_store(&pinInitializing, false);
    return result;
}

// --- ISR Handler ---

/**
 * @brief Central GPIO ISR handler.
 * Dispatches ISR-safe callbacks and copies events through the EventBus ISR boundary.
 */
static void IRAM_ATTR gpio_isr_handler(void *arg) {
    uintptr_t function_id = (uintptr_t)arg;
    if (function_id <= PIN_FUNC_NONE || function_id >= PIN_FUNC_MAX_COUNT) return;
    PinFunction_t func = (PinFunction_t)function_id;

    uint64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&pinISRLock);
    PinInternalInfo_t *info = &pinManagerInstance.pinMap[func];
    if (!info->dispatchEnabled || info->inFlight == UINT32_MAX ||
        (info->hasInterruptTime && now - info->lastInterruptTime < (uint64_t)info->debounceMs * 1000ULL)) {
        portEXIT_CRITICAL_ISR(&pinISRLock); return;
    }
    info->lastInterruptTime = now;
    info->hasInterruptTime = true;
    info->inFlight++;
    gpio_num_t pin = info->gpio;
    PinInterruptHandler_t handler = info->handler;
    void *handler_arg = info->handlerArg;
    IEventBus *bus = pinManagerInstance.eventBus;
    portEXIT_CRITICAL_ISR(&pinISRLock);
    bool level = gpio_get_level(pin);
    if (handler) handler(func, level, handler_arg);
    bool yield = false;
    if (bus && bus->PublishFromISR) {
        PinChangedEvent_t eventData = {
            .func = func,
            .pin = pin,
            .level = level,
            .timestamp = now
        };
        BusEvent event = {
            .eventName = PIN_EVENT_CHANGED,
            .type = EVENT_TYPE_INFO,
            .data = &eventData,
            .dataSize = sizeof(eventData)
        };
        // Publish through the ISR-safe EventBus boundary
        bus->PublishFromISR(event, &yield);
    }
    portENTER_CRITICAL_ISR(&pinISRLock);
    info->inFlight--;
    portEXIT_CRITICAL_ISR(&pinISRLock);
    if (yield) portYIELD_FROM_ISR();
}

// --- IPinManager methods ---
static unsigned PinManager_I2CLine(int function);

static gpio_num_t PinManager_GetPin(IPinManager *self, PinFunction_t func) {
  if (func <= PIN_FUNC_NONE || func >= PIN_FUNC_MAX_COUNT) return GPIO_NUM_NC;
  
  gpio_num_t pin;
  if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return GPIO_NUM_NC;
  pin = pinManagerInstance.pinMap[func].gpio;
  xSemaphoreGive(pinManagerInstance.mutex);
  return pin;
}

static int32_t PinManager_SetPin(IPinManager *self, PinFunction_t func, gpio_num_t pin) {
  if (atomic_load(&pinInitializing)) return -4;
  if (func <= PIN_FUNC_NONE || func >= PIN_FUNC_MAX_COUNT ||
      (pin != GPIO_NUM_NC && (pin < 0 || pin >= GPIO_NUM_MAX))) return -1;

  if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return -4;
  if (atomic_load(&pinInitializing)) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
  for (int other = 1; pin != GPIO_NUM_NC && other < PIN_FUNC_MAX_COUNT; ++other) {
    if (other == func || pinManagerInstance.pinMap[other].gpio != pin) continue;
    unsigned line = PinManager_I2CLine(func);
    if (!line || line != PinManager_I2CLine(other)) { xSemaphoreGive(pinManagerInstance.mutex); return -1; }
  }
  portENTER_CRITICAL(&pinISRLock);
  bool busy = pinManagerInstance.pinMap[func].gpio != pin &&
    (pinManagerInstance.pinMap[func].reserved || pinManagerInstance.pinMap[func].interruptAttached ||
     pinManagerInstance.pinMap[func].cleanupPending || pinManagerInstance.pinMap[func].inFlight);
  if (!busy) pinManagerInstance.pinMap[func].gpio = pin;
  portEXIT_CRITICAL(&pinISRLock);
  if (busy) {
    xSemaphoreGive(pinManagerInstance.mutex); return -4;
  }
  xSemaphoreGive(pinManagerInstance.mutex);
  
  ESP_LOGI(TAG, "Pin func %d assigned to GPIO %d", func, pin);
  return 0;
}

static int32_t PinManager_ConfigureInternal(IPinManager *self, PinFunction_t func, PinMode_t mode, PinPull_t pull, bool startup) {
    if (!startup && atomic_load(&pinInitializing)) return -4;
    if (func <= PIN_FUNC_NONE || func >= PIN_FUNC_MAX_COUNT ||
        mode < PIN_MODE_INPUT || mode > PIN_MODE_OPEN_DRAIN ||
        pull < PIN_PULL_NONE || pull > PIN_PULL_UP_DOWN) return -1;

    if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return -4;
    if (!startup && atomic_load(&pinInitializing)) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
    gpio_num_t gpio = pinManagerInstance.pinMap[func].gpio;
    portENTER_CRITICAL(&pinISRLock);
    bool busy = pinManagerInstance.pinMap[func].interruptAttached ||
                pinManagerInstance.pinMap[func].cleanupPending || pinManagerInstance.pinMap[func].inFlight;
    portEXIT_CRITICAL(&pinISRLock);
    if (busy) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
    if (gpio < 0 || gpio >= GPIO_NUM_MAX) { xSemaphoreGive(pinManagerInstance.mutex); return -2; }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = (mode == PIN_MODE_OUTPUT) ? GPIO_MODE_OUTPUT : 
                (mode == PIN_MODE_INPUT) ? GPIO_MODE_INPUT :
                (mode == PIN_MODE_INPUT_OUTPUT) ? GPIO_MODE_INPUT_OUTPUT : 
                (mode == PIN_MODE_OPEN_DRAIN) ? GPIO_MODE_INPUT_OUTPUT_OD : GPIO_MODE_OUTPUT_OD,
        .pull_up_en = (pull == PIN_PULL_UP || pull == PIN_PULL_UP_DOWN) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pull == PIN_PULL_DOWN || pull == PIN_PULL_UP_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE // Disabled by default
    };

    esp_err_t err = gpio_config(&io_conf);
    xSemaphoreGive(pinManagerInstance.mutex);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", gpio, esp_err_to_name(err));
        return -3;
    }

    return 0;
}

static int32_t PinManager_Configure(IPinManager *self, PinFunction_t func, PinMode_t mode, PinPull_t pull) {
    return PinManager_ConfigureInternal(self, func, mode, pull, false);
}

static int32_t PinManager_AttachInterrupt(IPinManager *self, PinFunction_t func, PinInterruptType_t type, PinInterruptHandler_t handler, void *arg) {
    if (atomic_load(&pinInitializing)) return -4;
    if (func <= PIN_FUNC_NONE || func >= PIN_FUNC_MAX_COUNT || type < PIN_INTR_DISABLE || type > PIN_INTR_HIGH_LEVEL) return -1;
    if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return -4;
    PinInternalInfo_t *info = &pinManagerInstance.pinMap[func];
    if (atomic_load(&pinInitializing)) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
    
    if (info->gpio < 0 || info->gpio >= GPIO_NUM_MAX) {
        xSemaphoreGive(pinManagerInstance.mutex);
        return -2;
    }

    portENTER_CRITICAL(&pinISRLock);
    bool busy = info->interruptAttached || info->cleanupPending || info->inFlight;
    bool identical = info->interruptAttached && info->handler == handler && info->handlerArg == arg && info->intrType == type;
    if (busy) {
        portEXIT_CRITICAL(&pinISRLock);
        xSemaphoreGive(pinManagerInstance.mutex); return identical ? 0 : -4;
    }
    info->dispatchEnabled = false;
    info->handler = handler;
    info->handlerArg = arg;
    info->intrType = type;
    info->hasInterruptTime = false;
    portEXIT_CRITICAL(&pinISRLock);

    esp_err_t err = gpio_set_intr_type(info->gpio, (gpio_int_type_t)type);
    if (err == ESP_OK) {
        err = gpio_isr_handler_add(info->gpio, gpio_isr_handler, (void *)(uintptr_t)func);
    }
    if (err == ESP_OK) {
        portENTER_CRITICAL(&pinISRLock);
        info->interruptAttached = info->dispatchEnabled = true;
        portEXIT_CRITICAL(&pinISRLock);
    } else {
        esp_err_t rollback = gpio_set_intr_type(info->gpio, GPIO_INTR_DISABLE);
        portENTER_CRITICAL(&pinISRLock);
        info->handler = NULL; info->handlerArg = NULL;
        info->cleanupPending = rollback != ESP_OK;
        info->intrType = PIN_INTR_DISABLE;
        portEXIT_CRITICAL(&pinISRLock);
    }
    xSemaphoreGive(pinManagerInstance.mutex);
    return err == ESP_OK ? 0 : -3;
}

static int32_t PinManager_DetachInterrupt(IPinManager *self, PinFunction_t func) {
    if (atomic_load(&pinInitializing)) return -4;
    if (func <= PIN_FUNC_NONE || func >= PIN_FUNC_MAX_COUNT) return -1;

    if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return -4;
    PinInternalInfo_t *info = &pinManagerInstance.pinMap[func];
    if (atomic_load(&pinInitializing)) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
    portENTER_CRITICAL(&pinISRLock);
    info->dispatchEnabled = false;
    bool attached = info->interruptAttached;
    portEXIT_CRITICAL(&pinISRLock);
    if (attached) {
        if (gpio_isr_handler_remove(info->gpio) != ESP_OK) {
            portENTER_CRITICAL(&pinISRLock); info->dispatchEnabled = true; portEXIT_CRITICAL(&pinISRLock);
            xSemaphoreGive(pinManagerInstance.mutex); return -3;
        }
        portENTER_CRITICAL(&pinISRLock);
        info->interruptAttached = false; info->cleanupPending = true;
        portEXIT_CRITICAL(&pinISRLock);
    }
    if (info->cleanupPending) {
        if (gpio_set_intr_type(info->gpio, GPIO_INTR_DISABLE) != ESP_OK) {
            xSemaphoreGive(pinManagerInstance.mutex); return -3;
        }
        portENTER_CRITICAL(&pinISRLock); info->cleanupPending = false; portEXIT_CRITICAL(&pinISRLock);
    }
    portENTER_CRITICAL(&pinISRLock);
    bool busy = info->inFlight != 0;
    if (!busy) { info->handler = NULL; info->handlerArg = NULL; info->intrType = PIN_INTR_DISABLE; }
    portEXIT_CRITICAL(&pinISRLock);
    xSemaphoreGive(pinManagerInstance.mutex);
    return busy ? -4 : 0;
}

static int32_t PinManager_SetDebounce(IPinManager *self, PinFunction_t func, uint32_t ms) {
    if (atomic_load(&pinInitializing)) return -4;
    if (func <= PIN_FUNC_NONE || func >= PIN_FUNC_MAX_COUNT) return -1;

    if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return -4;
    if (atomic_load(&pinInitializing)) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
    portENTER_CRITICAL(&pinISRLock);
    pinManagerInstance.pinMap[func].debounceMs = ms;
    portEXIT_CRITICAL(&pinISRLock);
    xSemaphoreGive(pinManagerInstance.mutex);

    return 0;
}

static unsigned PinManager_I2CLine(int function) {
  if (function == PIN_FUNC_MLX90614_SCL || function == PIN_FUNC_OLED_SCL || function == PIN_FUNC_I2C0_SCL) return 1;
  if (function == PIN_FUNC_MLX90614_SDA || function == PIN_FUNC_OLED_SDA || function == PIN_FUNC_I2C0_SDA) return 2;
  return 0;
}

static bool PinManager_ValidateInternal(IPinManager *self, bool startup) {
  if (!startup && atomic_load(&pinInitializing)) return false;
  bool is_valid = true;
  if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return false;
  if (!startup && atomic_load(&pinInitializing)) { xSemaphoreGive(pinManagerInstance.mutex); return false; }

  for (int i = 1; i < PIN_FUNC_MAX_COUNT; i++) {
    gpio_num_t gpio_i = pinManagerInstance.pinMap[i].gpio;
    if (gpio_i == GPIO_NUM_NC) continue;
    if (gpio_i < 0 || gpio_i >= GPIO_NUM_MAX ||
        (pinManagerInstance.pinMap[i].reserved && pinManagerInstance.pinMap[i].reservedGPIO != gpio_i)) {
      is_valid = false; continue;
    }

    for (int j = i + 1; j < PIN_FUNC_MAX_COUNT; j++) {
      gpio_num_t gpio_j = pinManagerInstance.pinMap[j].gpio;
      if (gpio_j == GPIO_NUM_NC) continue;

      // I2C sharing: devices on one bus may use the same pins
      unsigned line_i = PinManager_I2CLine(i), line_j = PinManager_I2CLine(j);
      if (gpio_i == gpio_j && !(line_i && line_i == line_j)) {
        ESP_LOGE(TAG, "Pin conflict: GPIO %d used by func %d and %d", gpio_i, i, j);
        is_valid = false;
      }
    }

  }
  bool acquired[PIN_FUNC_MAX_COUNT] = {false};
  IResourceRegistry *resources = pinManagerInstance.resourceRegistry;
  if (is_valid && resources && (!resources->Reserve || !resources->Release)) is_valid = false;
  for (int i = 1; is_valid && resources && i < PIN_FUNC_MAX_COUNT; ++i) {
    gpio_num_t gpio_i = pinManagerInstance.pinMap[i].gpio;
    if (gpio_i != GPIO_NUM_NC && !pinManagerInstance.pinMap[i].reserved) {
      ResourceId_t res = {.type = RESOURCE_TYPE_GPIO, .id = gpio_i};
      bool shared = (i == PIN_FUNC_MLX90614_SCL || i == PIN_FUNC_MLX90614_SDA ||
                     i == PIN_FUNC_OLED_SCL || i == PIN_FUNC_OLED_SDA ||
                     i == PIN_FUNC_I2C0_SCL || i == PIN_FUNC_I2C0_SDA);

      char owner[16];
      snprintf(owner, sizeof(owner), "PF_%d", i);
      if (resources->Reserve(resources, res, owner, shared) != 0) {
        ESP_LOGE(TAG, "GPIO %d reservation failed for func %d", gpio_i, i);
        is_valid = false;
      } else {
        acquired[i] = true;
        pinManagerInstance.pinMap[i].reserved = true;
        pinManagerInstance.pinMap[i].reservedGPIO = gpio_i;
      }
    }
  }
  if (!is_valid && resources && resources->Release) {
    for (int i = PIN_FUNC_MAX_COUNT - 1; i > PIN_FUNC_NONE; --i) {
      if (!acquired[i]) continue;
      char owner[16]; snprintf(owner, sizeof(owner), "PF_%d", i);
      ResourceId_t resource = {RESOURCE_TYPE_GPIO, pinManagerInstance.pinMap[i].reservedGPIO};
      if (resources->Release(resources, resource, owner) == 0) pinManagerInstance.pinMap[i].reserved = false;
      else ESP_LOGE(TAG, "Reservation rollback retained ownership for func %d", i);
    }
  }

  xSemaphoreGive(pinManagerInstance.mutex);
  if (is_valid) ESP_LOGI(TAG, "Pin mapping validation passed");
  return is_valid;
}

static bool PinManager_Validate(IPinManager *self) { return PinManager_ValidateInternal(self, false); }

// --- Service Lifecycle ---

static int32_t PinManager_InitializeWithConfig(IPinManager *self, PinManagerConfig config, IDIRegistry *registry) {
  if (atomic_exchange(&pinInitializing, true)) return -4;
  if (!pinManagerInstance.mutex || (registry && !registry->Resolve)) return PinManager_FinishInitialize(-1);
  for (int i = 1; i < PIN_FUNC_MAX_COUNT; ++i) {
    if ((config.entries[i].pin != GPIO_NUM_NC &&
         (config.entries[i].pin < 0 || config.entries[i].pin >= GPIO_NUM_MAX)) ||
        config.entries[i].mode < PIN_MODE_INPUT || config.entries[i].mode > PIN_MODE_OPEN_DRAIN ||
        config.entries[i].pull < PIN_PULL_NONE || config.entries[i].pull > PIN_PULL_UP_DOWN) return PinManager_FinishInitialize(-1);
    if (config.entries[i].pin != GPIO_NUM_NC) {
      for (int j = i + 1; j < PIN_FUNC_MAX_COUNT; ++j) {
        unsigned line_i = PinManager_I2CLine(i), line_j = PinManager_I2CLine(j);
        if (config.entries[i].pin == config.entries[j].pin && !(line_i && line_i == line_j))
          return PinManager_FinishInitialize(-1);
      }
    }
  }
  IResourceRegistry *resources = NULL;
  IEventBus *bus = NULL;
  if (registry) {
    resources = (IResourceRegistry *)registry->Resolve(registry, SVC_NAME_RESOURCE_REGISTRY);
    bus = (IEventBus *)registry->Resolve(registry, SVC_NAME_EVENT_BUS);
  }
  if (xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return PinManager_FinishInitialize(-4);
  if (pinManagerInstance.state == SERVICE_STATE_STARTED) {
    bool identical = hasAppliedPinConfig && atomic_load(&pinConfigurationApplied) &&
      PinManager_SameConfig(&config, &appliedPinConfig) &&
      (!registry || (resources == pinManagerInstance.resourceRegistry && bus == pinManagerInstance.eventBus));
    xSemaphoreGive(pinManagerInstance.mutex);
    return PinManager_FinishInitialize(identical ? 0 : -4);
  }
  portENTER_CRITICAL(&pinISRLock);
  bool busy = false;
  for (int i = 1; i < PIN_FUNC_MAX_COUNT; ++i) {
    if (pinManagerInstance.pinMap[i].reserved && pinManagerInstance.pinMap[i].reservedGPIO != config.entries[i].pin) {
      busy = true;
    }
    if (pinManagerInstance.pinMap[i].interruptAttached || pinManagerInstance.pinMap[i].inFlight ||
        pinManagerInstance.pinMap[i].cleanupPending) busy = true;
  }
  portEXIT_CRITICAL(&pinISRLock);
  xSemaphoreGive(pinManagerInstance.mutex);
  if (busy) return PinManager_FinishInitialize(-4);
  if (xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return PinManager_FinishInitialize(-4);
  for (int i = 1; registry && i < PIN_FUNC_MAX_COUNT; ++i)
    if (pinManagerInstance.pinMap[i].reserved && resources != pinManagerInstance.resourceRegistry) {
      xSemaphoreGive(pinManagerInstance.mutex); return PinManager_FinishInitialize(-4);
    }
  portENTER_CRITICAL(&pinISRLock);
  if (registry) { pinManagerInstance.resourceRegistry = resources; pinManagerInstance.eventBus = bus; }
  atomic_store(&pinConfigurationApplied, false);
  for (int i = 0; i < PIN_FUNC_MAX_COUNT; i++) {
    pinManagerInstance.pinMap[i].gpio = config.entries[i].pin;
    pinManagerInstance.pinMap[i].debounceMs = config.entries[i].debounceMs > 0 ? config.entries[i].debounceMs : config.defaultDebounceMs;
    pinManagerInstance.pinMap[i].handler = NULL;
    pinManagerInstance.pinMap[i].handlerArg = NULL;
    pinManagerInstance.pinMap[i].dispatchEnabled = false;
    pinManagerInstance.pinMap[i].hasInterruptTime = false;
    pinManagerInstance.pinMap[i].lastInterruptTime = 0;
  }
  portEXIT_CRITICAL(&pinISRLock);
  xSemaphoreGive(pinManagerInstance.mutex);

  // Install the ISR service once
  static bool isr_installed = false;
  if (!isr_installed) {
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        isr_installed = true;
    } else {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        return PinManager_FinishInitialize(-1);
    }
  }

  // Apply startup configuration
  for (int i = 1; i < PIN_FUNC_MAX_COUNT; i++) {
    if (config.entries[i].pin != GPIO_NUM_NC) {
        if (PinManager_ConfigureInternal(self, (PinFunction_t)i, config.entries[i].mode, config.entries[i].pull, true) != 0) {
            pinManagerInstance.state = SERVICE_STATE_ERROR;
            return PinManager_FinishInitialize(-3);
        }
        if (config.entries[i].mode == PIN_MODE_OUTPUT) {
            if (gpio_set_level(config.entries[i].pin, config.entries[i].initialState) != ESP_OK) {
                pinManagerInstance.state = SERVICE_STATE_ERROR;
                return PinManager_FinishInitialize(-3);
            }
        }
    }
  }

  if (!PinManager_ValidateInternal(&pinManagerInstance.base, true)) return PinManager_FinishInitialize(-2);

  if (xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return PinManager_FinishInitialize(-4);
  appliedPinConfig = config;
  hasAppliedPinConfig = true;
  pinManagerInstance.state = SERVICE_STATE_INITIALIZED;
  atomic_store(&pinConfigurationApplied, true);
  xSemaphoreGive(pinManagerInstance.mutex);
  return PinManager_FinishInitialize(0);
}

static int32_t PinManager_Initialize(IService *self, IDIRegistry *registry) {
  return PinManager_InitializeWithConfig(&pinManagerInstance.base, PinManagerConfig_GetDefault(), registry);
}

static int32_t PinManager_Start(IService *self) {
  if (atomic_load(&pinInitializing)) return -4;
  if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return -4;
  if (atomic_load(&pinInitializing) || !atomic_load(&pinConfigurationApplied)) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
  if (pinManagerInstance.state != SERVICE_STATE_INITIALIZED && pinManagerInstance.state != SERVICE_STATE_STARTED &&
      pinManagerInstance.state != SERVICE_STATE_STOPPED) { xSemaphoreGive(pinManagerInstance.mutex); return -1; }
  ESP_LOGI(TAG, "Starting Pin Manager service");
  pinManagerInstance.state = SERVICE_STATE_STARTED;
  xSemaphoreGive(pinManagerInstance.mutex);
  return 0;
}

static int32_t PinManager_Stop(IService *self) {
  if (atomic_load(&pinInitializing)) return -4;
  if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return -4;
  if (atomic_load(&pinInitializing)) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
  portENTER_CRITICAL(&pinISRLock);
  bool busy = false;
  for (int i = 1; i < PIN_FUNC_MAX_COUNT; ++i)
      if (pinManagerInstance.pinMap[i].interruptAttached || pinManagerInstance.pinMap[i].cleanupPending ||
          pinManagerInstance.pinMap[i].inFlight || pinManagerInstance.pinMap[i].handler || pinManagerInstance.pinMap[i].handlerArg) busy = true;
  portEXIT_CRITICAL(&pinISRLock);
  if (busy) { xSemaphoreGive(pinManagerInstance.mutex); return -4; }
  pinManagerInstance.state = SERVICE_STATE_STOPPED;
  xSemaphoreGive(pinManagerInstance.mutex);
  return 0;
}

static ServiceState PinManager_GetState(IService *self) {
  if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return SERVICE_STATE_ERROR;
  ServiceState state = pinManagerInstance.state;
  xSemaphoreGive(pinManagerInstance.mutex);
  return state;
}

static int32_t PinManager_CheckHealth(IService *self) {
  return !atomic_load(&pinInitializing) && atomic_load(&pinConfigurationApplied) && PinManager_GetState(self) == SERVICE_STATE_STARTED ? 0 : -1;
}

// --- Console Command ---

static const char* pin_func_to_str(PinFunction_t func, const LocalizationCatalog *catalog) {
    switch(func) {
        case PIN_FUNC_I2C0_SDA:    return Localization_Text(catalog, LOC_CLI_PIN_MAP_I2C_SDA);
        case PIN_FUNC_I2C0_SCL:    return Localization_Text(catalog, LOC_CLI_PIN_MAP_I2C_SCL);
        case PIN_FUNC_MLX90614_SDA: return Localization_Text(catalog, LOC_CLI_PIN_MAP_SENSOR_SDA);
        case PIN_FUNC_MLX90614_SCL: return Localization_Text(catalog, LOC_CLI_PIN_MAP_SENSOR_SCL);
        case PIN_FUNC_OLED_SDA:    return Localization_Text(catalog, LOC_CLI_PIN_MAP_DISPLAY_SDA);
        case PIN_FUNC_OLED_SCL:    return Localization_Text(catalog, LOC_CLI_PIN_MAP_DISPLAY_SCL);
        case PIN_FUNC_ROTARY_CLK:  return Localization_Text(catalog, LOC_CLI_PIN_MAP_ENCODER_CLOCK);
        case PIN_FUNC_ROTARY_DT:   return Localization_Text(catalog, LOC_CLI_PIN_MAP_ENCODER_DATA);
        case PIN_FUNC_ROTARY_SW:   return Localization_Text(catalog, LOC_CLI_PIN_MAP_ENCODER_BUTTON);
        case PIN_FUNC_RELAY:       return Localization_Text(catalog, LOC_CLI_PIN_MAP_FAN_OUTPUT);
        case PIN_FUNC_RGB_RED:     return Localization_Text(catalog, LOC_CLI_PIN_MAP_RED_CHANNEL);
        case PIN_FUNC_RGB_GREEN:   return Localization_Text(catalog, LOC_CLI_PIN_MAP_GREEN_CHANNEL);
        case PIN_FUNC_RGB_BLUE:    return Localization_Text(catalog, LOC_CLI_PIN_MAP_BLUE_CHANNEL);
        case PIN_FUNC_BUILTIN_LED: return Localization_Text(catalog, LOC_CLI_PIN_MAP_STATUS_LED);
        default: return Localization_Text(catalog, LOC_CLI_PIN_MAP_UNKNOWN);
    }
}

static int32_t PinManager_DumpPinMap(IPinManager *self) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!self) return -1;
    gpio_num_t pins[PIN_FUNC_MAX_COUNT];
    if (!pinManagerInstance.mutex || xSemaphoreTake(pinManagerInstance.mutex, pdMS_TO_TICKS(PIN_MANAGER_LOCK_TIMEOUT_MS)) != pdTRUE) return -4;
    for (int i = 0; i < PIN_FUNC_MAX_COUNT; ++i) pins[i] = pinManagerInstance.pinMap[i].gpio;
    xSemaphoreGive(pinManagerInstance.mutex);
    
    printf("\n" CLI_COLOR_CYAN "+----------------------+-----------------------+" CLI_COLOR_RESET "\n");
    LOC_PRINTF(catalog, LOC_CLI_PIN_MAP_HEADER, "| Pin function         | GPIO                |\n");
    printf(CLI_COLOR_CYAN "+----------------------+-----------------------+" CLI_COLOR_RESET "\n");
    
    for (int i = 1; i < PIN_FUNC_MAX_COUNT; i++) {
        gpio_num_t pin = pins[i];
        if (pin != GPIO_NUM_NC) {
            printf(CLI_COLOR_CYAN "|" CLI_COLOR_RESET " %-20s " CLI_COLOR_CYAN "|" CLI_COLOR_RESET "  %-19d " CLI_COLOR_CYAN "|" CLI_COLOR_RESET "\n", 
                   pin_func_to_str(i, catalog), pin);
        }
    }
    
    printf(CLI_COLOR_CYAN "+----------------------+-----------------------+" CLI_COLOR_RESET "\n\n");
    return 0;
}

// --- Instance ---

IPinManager *PinManager_GetInstance(void) {
  static bool initialized = false;
  if (!PinManager_EnsureMutex()) return NULL;
  portENTER_CRITICAL(&pinInstallMux);
  if (!initialized) {
    initialized = true;
    
    pinManagerInstance.base.base.Initialize = PinManager_Initialize;
    pinManagerInstance.base.base.Start = PinManager_Start;
    pinManagerInstance.base.base.Stop = PinManager_Stop;
    pinManagerInstance.base.base.GetState = PinManager_GetState;
    pinManagerInstance.base.base.CheckHealth = PinManager_CheckHealth;

    pinManagerInstance.base.InitializeWithConfig = PinManager_InitializeWithConfig;
    pinManagerInstance.base.GetPin = PinManager_GetPin;
    pinManagerInstance.base.SetPin = PinManager_SetPin;
    pinManagerInstance.base.Configure = PinManager_Configure;
    pinManagerInstance.base.AttachInterrupt = PinManager_AttachInterrupt;
    pinManagerInstance.base.DetachInterrupt = PinManager_DetachInterrupt;
    pinManagerInstance.base.SetDebounce = PinManager_SetDebounce;
    pinManagerInstance.base.DumpPinMap = PinManager_DumpPinMap;
    pinManagerInstance.base.Validate = PinManager_Validate;

    pinManagerInstance.state = SERVICE_STATE_UNINITIALIZED;
  }
  portEXIT_CRITICAL(&pinInstallMux);
  return &pinManagerInstance.base;
}
