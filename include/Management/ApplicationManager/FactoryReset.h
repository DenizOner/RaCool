#pragma once

#include "Communication/Zigbee/IZigbee.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/PowerManager/IPowerManager.h"
#include "Management/ApplicationManager/ApplicationManagerConfig.h"

typedef enum {
    FACTORY_RESET_IDLE,
    FACTORY_RESET_PREFLIGHT,
    FACTORY_RESET_SETTINGS,
    FACTORY_RESET_CPU,
    FACTORY_RESET_RADIO,
    FACTORY_RESET_NETWORK,
    FACTORY_RESET_WAIT_NETWORK,
    FACTORY_RESET_VERIFY,
    FACTORY_RESET_RESTART_REQUESTED,
    FACTORY_RESET_FAILED
} FactoryResetPhase;

/* Owned by the existing application worker. No callback may advance it. */
typedef struct {
    FactoryResetPhase phase;
    esp_err_t error;
    uint32_t started_ms;
    uint32_t network_receipt;
    bool guard_held;
    uint32_t (*ClockMs)(void);
    IConfigurationManager *config;
    IPowerManager *power;
    IZigbee *radio;
    SystemSettings defaults;
} FactoryResetOperation;

static inline esp_err_t FactoryReset_CheckProfiles(FactoryResetOperation *operation) {
    if (operation->power->GetPowerMode(operation->power) !=
        (PowerMode_t)operation->defaults.system.power_profile) return ESP_ERR_INVALID_STATE;
    esp_err_t result = operation->power->SelfTest(operation->power);
    if (result != POWER_OK) return result;
    ZigbeeTxStatus status;
    result = operation->radio->GetTxStatus(operation->radio, &status);
    if (result != ESP_OK) return result;
    return status.profile == (ZigbeeTxProfile)operation->defaults.system.zigbee_tx_profile
        ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/* One bounded owner operation per turn. Synchronous SDK/NVS calls cannot be
 * preempted by the elapsed deadline; never kill their task to force a timeout. */
static inline void FactoryReset_Advance(FactoryResetOperation *operation, uint32_t now_ms) {
    if (!operation || operation->phase == FACTORY_RESET_IDLE ||
        operation->phase >= FACTORY_RESET_RESTART_REQUESTED) return;
    esp_err_t result = ESP_OK;
    if ((uint32_t)(now_ms - operation->started_ms) >= APP_FACTORY_RESET_TIMEOUT_MS) {
        result = ESP_ERR_TIMEOUT;
    } else switch (operation->phase) {
        case FACTORY_RESET_PREFLIGHT: {
            IConfigurationManager *config = operation->config;
            IPowerManager *power = operation->power;
            IZigbee *radio = operation->radio;
            if (!config || !config->ResetToDefaults || !config->VerifyDefaults || !config->SetFactoryResetGuard ||
                !power || !power->SetPowerMode || !power->GetPowerMode ||
                !power->SelfTest || !power->RestartAsync || !power->IsTransitionPending ||
                !radio || !radio->SetTxProfile || !radio->GetTxStatus ||
                !radio->BeginFactoryReset || !radio->GetFactoryResetStatus || !radio->IsPairing) {
                result = ESP_ERR_INVALID_STATE;
                break;
            }
            result = config->SetFactoryResetGuard(config, true);
            if (result != ESP_OK) break;
            operation->guard_held = true;
            if (power->IsTransitionPending(power)) { result = ESP_ERR_INVALID_STATE; break; }
            ZigbeeFactoryResetStatus status;
            result = radio->GetFactoryResetStatus(radio, &status);
            if (result == ESP_OK && (status.pending || radio->IsPairing(radio) ||
                (!status.connected && !status.factory_new) ||
                (status.connected && status.factory_new))) result = ESP_ERR_INVALID_STATE;
            if (result == ESP_OK) operation->phase = FACTORY_RESET_SETTINGS;
            break;
        }
        case FACTORY_RESET_SETTINGS:
            result = operation->config->ResetToDefaults(operation->config, NULL);
            if (result == ESP_OK) result = operation->config->VerifyDefaults(operation->config, &operation->defaults);
            if (result == ESP_OK) operation->phase = FACTORY_RESET_CPU;
            break;
        case FACTORY_RESET_CPU:
            result = operation->power->SetPowerMode(operation->power,
                (PowerMode_t)operation->defaults.system.power_profile);
            if (result == ESP_OK) operation->phase = FACTORY_RESET_RADIO;
            break;
        case FACTORY_RESET_RADIO:
            result = operation->radio->SetTxProfile(operation->radio,
                (ZigbeeTxProfile)operation->defaults.system.zigbee_tx_profile);
            if (result == ESP_OK) result = FactoryReset_CheckProfiles(operation);
            if (result == ESP_OK) operation->phase = FACTORY_RESET_NETWORK;
            break;
        case FACTORY_RESET_NETWORK:
            result = operation->radio->BeginFactoryReset(operation->radio, &operation->network_receipt);
            if (result == ESP_OK && !operation->network_receipt) result = ESP_ERR_INVALID_STATE;
            if (result == ESP_OK) operation->phase = FACTORY_RESET_WAIT_NETWORK;
            break;
        case FACTORY_RESET_WAIT_NETWORK: {
            ZigbeeFactoryResetStatus status;
            result = operation->radio->GetFactoryResetStatus(operation->radio, &status);
            if (result == ESP_OK && status.operation_id != operation->network_receipt) result = ESP_ERR_INVALID_STATE;
            if (result == ESP_OK && !status.pending) {
                result = status.result;
                if (result == ESP_OK && (!status.factory_new || status.connected)) result = ESP_ERR_INVALID_STATE;
                if (result == ESP_OK) operation->phase = FACTORY_RESET_VERIFY;
            }
            break;
        }
        case FACTORY_RESET_VERIFY: {
            SystemSettings verified;
            result = operation->config->VerifyDefaults(operation->config, &verified);
            if (result == ESP_OK) {
                operation->defaults = verified;
                result = FactoryReset_CheckProfiles(operation);
            }
            ZigbeeFactoryResetStatus status;
            if (result == ESP_OK) result = operation->radio->GetFactoryResetStatus(operation->radio, &status);
            if (result == ESP_OK && (status.operation_id != operation->network_receipt || status.pending ||
                status.result != ESP_OK || !status.factory_new || status.connected)) result = ESP_ERR_INVALID_STATE;
            if (result == ESP_OK && operation->ClockMs &&
                (uint32_t)(operation->ClockMs() - operation->started_ms) >= APP_FACTORY_RESET_TIMEOUT_MS)
                result = ESP_ERR_TIMEOUT;
            if (result == ESP_OK) result = operation->power->RestartAsync(operation->power, 0);
            if (result == ESP_OK) operation->phase = FACTORY_RESET_RESTART_REQUESTED;
            break;
        }
        default: result = ESP_ERR_INVALID_STATE; break;
    }
    if (result != ESP_OK) {
        operation->error = result;
        operation->phase = FACTORY_RESET_FAILED;
    }
}

