#include "infrared_fixture.h"

// The removed generic sensor interface is covered through the actual MLX90614 service.
void setUp(void) { IRFixture_SetUp(); }
void tearDown(void) { IRFixture_TearDown(); }

static IInfraredTemperatureSensor *start_sensor(void) {
    IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
    TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
    return sensor;
}

void test_temperature_sensor_start(void) {
    IInfraredTemperatureSensor *sensor = start_sensor();
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, sensor->base.GetState(&sensor->base));
}
void test_temperature_sensor_stop(void) {
    IInfraredTemperatureSensor *sensor = start_sensor();
    TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, sensor->base.GetState(&sensor->base));
}
void test_temperature_sensor_read(void) {
    IInfraredTemperatureSensor *sensor = start_sensor();
    mock_read_data[0] = 0x3C;
    mock_read_data[1] = 0x3A;
    mock_read_data[2] = 0xFF;
    sensor->TriggerMeasurement(sensor);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 25.01f, sensor->ReadObjectTemperature(sensor));
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 25.01f, sensor->ReadAmbientTemperature(sensor));
}
void test_temperature_sensor_health_check(void) {
    IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
    TEST_ASSERT_NOT_EQUAL(0, sensor->base.CheckHealth(&sensor->base));
    start_sensor();
    TEST_ASSERT_EQUAL(0, sensor->base.CheckHealth(&sensor->base));
    mock_read_register_err = ESP_FAIL;
    for (int i = 0; i < 3; ++i) sensor->TriggerMeasurement(sensor);
    TEST_ASSERT_NOT_EQUAL(0, sensor->base.CheckHealth(&sensor->base));
}
void test_temperature_sensor_read_uninitialized(void) {
    IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL_FLOAT(IRTEMP_CACHE_UNINITIALIZED, sensor->ReadObjectTemperature(sensor));
    TEST_ASSERT_EQUAL_FLOAT(IRTEMP_CACHE_UNINITIALIZED, sensor->ReadAmbientTemperature(sensor));
    TEST_ASSERT_FALSE(event_published);
}
