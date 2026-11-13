#ifndef TEST_ZIGBEE_MOCK_EVENTBUS_H
#define TEST_ZIGBEE_MOCK_EVENTBUS_H

#include "Core/EventBus/IEventBus.h"

extern int mock_publish_called;
extern const char *last_event_name;

int32_t mock_publish(BusEvent e);

extern IEventBus mock_event_bus;
extern SystemSettings mock_settings;
extern IConfigurationManager mock_config_inst;
SystemSettings *mock_get_settings(IConfigurationManager *self);
esp_err_t mock_get_snapshot(IConfigurationManager *self, SystemSettings *output);

#endif // TEST_ZIGBEE_MOCK_EVENTBUS_H
