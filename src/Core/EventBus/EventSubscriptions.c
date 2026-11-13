#include "Core/EventBus/EventSubscriptions.h"
#include "esp_err.h"
#include <string.h>

static bool ValidTable(const EventSubscription *table, size_t count) {
    if (!table || !count || count > 32) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!table[i].name || !table[i].name[0] || !table[i].handler) return false;
        for (size_t j = 0; j < i; ++j)
            if (table[i].handler == table[j].handler && strcmp(table[i].name, table[j].name) == 0) return false;
    }
    return true;
}

int32_t EventSubscriptions_Detach(EventSubscriptions *state,
        const EventSubscription *table, size_t count) {
    if (!state || !ValidTable(table, count)) return ESP_ERR_INVALID_ARG;
    if (!state->owned) { *state = (EventSubscriptions){0}; return ESP_OK; }
    if (state->table != table || state->count != count) return ESP_ERR_INVALID_STATE;
    if (!state->bus || !state->bus->Unsubscribe) return ESP_ERR_INVALID_STATE;
    if (count < 32 && (state->owned >> count)) return ESP_ERR_INVALID_STATE;
    int32_t result = ESP_OK;
    for (size_t i = 0; i < count; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if (!(state->owned & bit)) continue;
        int32_t removal = state->bus->Unsubscribe(state->bus, table[i].name, table[i].handler);
        if (removal == 0) state->owned &= ~bit;
        else if (result == ESP_OK) result = removal;
    }
    if (!state->owned) *state = (EventSubscriptions){0};
    return result;
}

int32_t EventSubscriptions_Attach(EventSubscriptions *state, IEventBus *bus,
        const EventSubscription *table, size_t count) {
    if (!state || !ValidTable(table, count)) return ESP_ERR_INVALID_ARG;
    if (!bus || !bus->Subscribe || !bus->Unsubscribe) return ESP_ERR_INVALID_STATE;
    if (state->owned && (state->bus != bus || state->table != table || state->count != count))
        return ESP_ERR_INVALID_STATE;
    if (count < 32 && (state->owned >> count)) return ESP_ERR_INVALID_STATE;
    state->bus = bus;
    state->table = table;
    state->count = count;
    for (size_t i = 0; i < count; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if (state->owned & bit) continue;
        int32_t result = bus->Subscribe(bus, table[i].name, table[i].handler);
        if (result != 0) {
            /* Preserve every subscription that rollback cannot safely remove. */
            (void)EventSubscriptions_Detach(state, table, count);
            return result;
        }
        state->owned |= bit;
    }
    return ESP_OK;
}
