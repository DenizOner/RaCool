#include "test_zigbee_shared.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
DEFINE_FAKE_VALUE_FUNC1(uint8_t, mocked_ezb_af_ep_desc_get_ep_id, ezb_af_ep_desc_t);
DEFINE_FAKE_VALUE_FUNC1(uint16_t, mocked_ezb_zcl_cluster_desc_get_id, ezb_zcl_cluster_desc_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_attr_desc_t, mocked_ezb_zcl_cluster_get_next_attr_desc, ezb_zcl_cluster_desc_t, ezb_zcl_attr_desc_t);
DEFINE_FAKE_VALUE_FUNC1(uint16_t, mocked_ezb_zcl_attr_desc_get_id, ezb_zcl_attr_desc_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_zcl_attr_access_t, mocked_ezb_zcl_attr_desc_get_access, ezb_zcl_attr_desc_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_err_t, mocked_ezb_zcl_attr_desc_set_access, ezb_zcl_attr_desc_t, ezb_zcl_attr_access_t);

DEFINE_FAKE_VALUE_FUNC0(IRGBLED *, mocked_GetRGBLEDService);
DEFINE_FAKE_VALUE_FUNC2(ezb_err_t, mocked_ezb_zcl_attr_desc_get_value, ezb_zcl_attr_desc_t, void *);
DEFINE_FAKE_VALUE_FUNC1(uint16_t, mocked_ezb_zcl_attr_desc_get_value_size, ezb_zcl_attr_desc_t);
DEFINE_FAKE_VALUE_FUNC5(ezb_zcl_attr_desc_t, mocked_ezb_zcl_get_attr_desc, uint8_t, uint16_t, uint8_t, uint16_t, uint16_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_zcl_attr_type_t, mocked_ezb_zcl_attr_desc_get_type, ezb_zcl_attr_desc_t);
DEFINE_FAKE_VOID_FUNC1(mocked_ezb_af_free_endpoint_desc, ezb_af_ep_desc_t);
DEFINE_FAKE_VOID_FUNC1(mocked_ezb_zcl_free_cluster_desc, ezb_zcl_cluster_desc_t);
DEFINE_FAKE_VALUE_FUNC0(ezb_af_device_desc_t, mocked_ezb_af_create_device_desc);
DEFINE_FAKE_VALUE_FUNC2(ezb_err_t, mocked_ezb_af_device_add_endpoint_desc, ezb_af_device_desc_t, ezb_af_ep_desc_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_af_device_desc_register, ezb_af_device_desc_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_af_ep_desc_t, mocked_ezb_af_create_endpoint_desc, const ezb_af_ep_config_t *);
DEFINE_FAKE_VALUE_FUNC2(ezb_err_t, mocked_ezb_af_endpoint_add_cluster_desc, ezb_af_ep_desc_t, ezb_zcl_cluster_desc_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_bdb_set_primary_channel_set, uint32_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_bdb_set_secondary_channel_set, uint32_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_bdb_start_top_level_commissioning, ezb_bdb_comm_mode_mask_t);
DEFINE_FAKE_VOID_FUNC0(mocked_ezb_bdb_reset_via_local_action);
DEFINE_FAKE_VALUE_FUNC0(bool, mocked_ezb_bdb_is_factory_new);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_dev_start, bool);
DEFINE_FAKE_VALUE_FUNC0(ezb_shortaddr_t, mocked_ezb_get_short_address);
DEFINE_FAKE_VOID_FUNC1(mocked_ezb_get_extended_panid, ezb_extpanid_t *);
DEFINE_FAKE_VALUE_FUNC0(ezb_panid_t, mocked_ezb_get_panid);
DEFINE_FAKE_VALUE_FUNC0(uint8_t, mocked_ezb_get_current_channel);
DEFINE_FAKE_VOID_FUNC1(mocked_ezb_set_tx_power, int8_t);
DEFINE_FAKE_VOID_FUNC1(mocked_ezb_get_tx_power, int8_t *);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_nwk_set_max_children, uint8_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_nwk_set_device_type, ezb_nwk_device_type_t);
DEFINE_FAKE_VALUE_FUNC0(ezb_nwk_device_type_t, mocked_ezb_nwk_get_device_type);
DEFINE_FAKE_VALUE_FUNC2(ezb_err_t, mocked_ezb_nwk_get_next_neighbor, ezb_nwk_info_iterator_t *, ezb_nwk_neighbor_info_t *);
DEFINE_FAKE_VALUE_FUNC3(ezb_err_t, mocked_ezb_zcl_analog_input_cluster_desc_add_attr, ezb_zcl_cluster_desc_t, uint16_t, const void *);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_analog_input_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC3(ezb_err_t, mocked_ezb_zcl_basic_cluster_desc_add_attr, ezb_zcl_cluster_desc_t, uint16_t, const void *);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_basic_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_color_control_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_groups_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_identify_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_level_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_on_off_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC3(ezb_err_t, mocked_ezb_zcl_ota_upgrade_cluster_desc_add_attr, ezb_zcl_cluster_desc_t, uint16_t, const void *);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_ota_upgrade_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_scenes_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_temperature_measurement_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC3(ezb_err_t, mocked_ezb_zcl_thermostat_cluster_desc_add_attr, ezb_zcl_cluster_desc_t, uint16_t, const void *);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_thermostat_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC3(ezb_err_t, mocked_ezb_zcl_thermostat_ui_config_cluster_desc_add_attr, ezb_zcl_cluster_desc_t, uint16_t, const void            *);
DEFINE_FAKE_VALUE_FUNC2(ezb_zcl_cluster_desc_t, mocked_ezb_zcl_thermostat_ui_config_create_cluster_desc, const void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC7(ezb_zcl_status_t, mocked_ezb_zcl_set_attr_value, uint8_t, uint16_t, uint8_t, uint16_t, uint16_t, void    *, bool);
DEFINE_FAKE_VOID_FUNC1(mocked_ezb_zcl_core_action_handler_register, ezb_zcl_core_action_callback_t);
DEFINE_FAKE_VALUE_FUNC6(ezb_err_t, mocked_ezb_zcl_cluster_desc_add_manuf_attr, ezb_zcl_cluster_desc_t, uint16_t, uint8_t, uint8_t, uint16_t, const void             *);
DEFINE_FAKE_VALUE_FUNC5(ezb_zcl_reporting_info_t, mocked_ezb_zcl_reporting_info_find, uint8_t, uint16_t, uint8_t, uint16_t, uint16_t);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_zcl_reporting_start_attr_report, ezb_zcl_reporting_info_t);
DEFINE_FAKE_VALUE_FUNC4(ezb_err_t, mocked_ezb_zcl_reporting_info_update, ezb_zcl_reporting_info_t, uint16_t, uint16_t, const ezb_zcl_attr_variable_t *);
DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_zdo_ieee_addr_req, const ezb_zdo_ieee_addr_req_t *);

#include "test_zigbee_mock_eventbus.h"
int mock_publish_called;
const char *last_event_name;
int32_t mock_publish(BusEvent event) { ++mock_publish_called; last_event_name=event.eventName; return 0; }
IEventBus mock_event_bus;
SystemSettings mock_settings;
IConfigurationManager mock_config_inst;
SystemSettings *mock_get_settings(IConfigurationManager *self) { return &mock_settings; }
esp_err_t mock_get_snapshot(IConfigurationManager *self, SystemSettings *output) {
    (void)self;
    *output = mock_settings;
    return ESP_OK;
}
ezb_af_ep_config_t last_endpoint_config;

static ezb_af_ep_desc_t capture_endpoint(const ezb_af_ep_config_t *config) {
    last_endpoint_config=*config;
    return (ezb_af_ep_desc_t)0x100;
}
static void fill_panid(ezb_extpanid_t *value) { memset(value, 0, sizeof(*value)); }
static int8_t configured_power;
static void store_power(int8_t value) { configured_power = value; }
static void read_power(int8_t *value) { *value = configured_power; }

void ZigbeeTest_Reset(void) {
    RESET_FAKE(mocked_ezb_af_ep_desc_get_ep_id);
    mocked_ezb_af_ep_desc_get_ep_id_fake.return_val = ZIGBEE_EP_SENSOR_FAN;
    RESET_FAKE(mocked_ezb_zcl_cluster_desc_get_id);
    RESET_FAKE(mocked_ezb_zcl_cluster_get_next_attr_desc);
    RESET_FAKE(mocked_ezb_zcl_attr_desc_get_id);
    RESET_FAKE(mocked_ezb_zcl_attr_desc_get_access);
    RESET_FAKE(mocked_ezb_zcl_attr_desc_set_access);

    RESET_FAKE(mocked_GetRGBLEDService);
    RESET_FAKE(mocked_ezb_zcl_attr_desc_get_value);
    RESET_FAKE(mocked_ezb_zcl_attr_desc_get_value_size);
    mocked_ezb_zcl_attr_desc_get_value_size_fake.return_val = sizeof(uint16_t);
    RESET_FAKE(mocked_ezb_zcl_get_attr_desc);
    RESET_FAKE(mocked_ezb_zcl_attr_desc_get_type);
    mocked_ezb_zcl_get_attr_desc_fake.return_val = (ezb_zcl_attr_desc_t)1;
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT16;
    RESET_FAKE(mocked_ezb_get_tx_power);
    configured_power = 10;
    mocked_ezb_set_tx_power_fake.custom_fake = store_power;
    mocked_ezb_get_tx_power_fake.custom_fake = read_power;
    mocked_ezb_zcl_basic_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_identify_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_temperature_measurement_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_thermostat_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_on_off_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_level_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_color_control_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_analog_input_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_groups_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_scenes_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_zcl_ota_upgrade_create_cluster_desc_fake.return_val = (void *)0x300;
    RESET_FAKE(mocked_ezb_af_free_endpoint_desc);
    RESET_FAKE(mocked_ezb_zcl_free_cluster_desc);
    mocked_ezb_zcl_thermostat_ui_config_create_cluster_desc_fake.return_val = (void *)0x300;
    RESET_FAKE(mocked_ezb_app_signal_to_string);
    mocked_ezb_get_extended_panid_fake.custom_fake = fill_panid;
    RESET_FAKE(mocked_ConfigurationManager_GetInstance);
    mock_config_inst.GetSnapshot = mock_get_snapshot;
    mocked_ConfigurationManager_GetInstance_fake.return_val = &mock_config_inst;
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ezb_af_create_endpoint_desc_fake.custom_fake=capture_endpoint;
}

DEFINE_FAKE_VALUE_FUNC1(const char *, mocked_ezb_app_signal_to_string, ezb_app_signal_type_t);

DEFINE_FAKE_VALUE_FUNC1(ezb_err_t, mocked_ezb_zcl_reporting_stop_attr_report, ezb_zcl_reporting_info_t);
