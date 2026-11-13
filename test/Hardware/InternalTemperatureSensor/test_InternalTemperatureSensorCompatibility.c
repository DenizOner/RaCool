/* Legacy lifecycle/read scenarios updated to the current IService and sensor API. */
#include "internal_temp_fixture.h"
#include <math.h>
static IInternalTemperatureSensor *sensor;
void setUp(void) { InternalTempFixture_SetUp(); sensor=InternalTemperatureSensor_GetModule(); }
void tearDown(void) { InternalTempFixture_TearDown(); }
static void initialize(void) { TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL)); }
static void start(void) { initialize(); TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base)); }
void test_InternalTemperatureSensor_GetModule_ShouldReturnNonNull(void) {
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL_PTR(sensor, InternalTemperatureSensor_GetModule());
}
void test_InternalTemperatureSensor_Initialize_ShouldInitializeSuccessfully(void) {
    initialize();
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, sensor->base.GetState(&sensor->base));
}
void test_InternalTemperatureSensor_Initialize_ShouldBeIdempotent(void) {
    initialize(); initialize();
    TEST_ASSERT_EQUAL(1, mocked_temperature_sensor_install_fake.call_count);
}
void test_InternalTemperatureSensor_Start_ShouldStartSuccessfully(void) {
    start();
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, sensor->base.GetState(&sensor->base));
}
void test_InternalTemperatureSensor_Start_ShouldFailWhenNotInitialized(void) {
    TEST_ASSERT_EQUAL(-1, sensor->base.Start(&sensor->base));
    TEST_ASSERT_EQUAL(0, mocked_temperature_sensor_enable_fake.call_count);
}
void test_InternalTemperatureSensor_Stop_ShouldStopSuccessfully(void) {
    start();
    TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, sensor->base.GetState(&sensor->base));
}
void test_InternalTemperatureSensor_Stop_ShouldFailWhenNotStarted(void) {
    initialize();
    TEST_ASSERT_EQUAL(-1, sensor->base.Stop(&sensor->base));
    TEST_ASSERT_EQUAL(0, mocked_temperature_sensor_disable_fake.call_count);
}
void test_InternalTemperatureSensor_GetState_ShouldReturnCorrectState(void) {
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, sensor->base.GetState(&sensor->base));
    initialize();
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, sensor->base.GetState(&sensor->base));
}
void test_InternalTemperatureSensor_CheckHealth_ShouldReturn0WhenStarted(void) {
    start(); TEST_ASSERT_EQUAL(0, sensor->base.CheckHealth(&sensor->base));
}
void test_InternalTemperatureSensor_CheckHealth_ShouldReturnMinus1WhenNotStarted(void) {
    initialize(); TEST_ASSERT_EQUAL(-1, sensor->base.CheckHealth(&sensor->base));
}
void test_InternalTemperatureSensor_ReadTemperature_ShouldReturnValidTemperature(void) {
    start(); mock_temp_val=37.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 37.5f, sensor->ReadTemperature(sensor));
}
void test_InternalTemperatureSensor_ReadTemperature_ShouldReturnReasonableValues(void) {
    start(); const float values[]={25.0f, 26.0f, 24.0f};
    for (unsigned i=0; i<3; ++i) {
        mock_temp_val=values[i];
        TEST_ASSERT_FLOAT_WITHIN(0.001f, values[i], sensor->ReadTemperature(sensor));
    }
}
void test_InternalTemperatureSensor_TemperatureNotNaN(void) {
    start(); TEST_ASSERT_FALSE(isnan(sensor->ReadTemperature(sensor)));
}
void test_InternalTemperatureSensor_TemperatureNotInfinity(void) {
    start(); TEST_ASSERT_FALSE(isinf(sensor->ReadTemperature(sensor)));
}
void test_InternalTemperatureSensor_RepeatedReads(void) {
    start();
    for (unsigned i=0; i<5; ++i) TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, sensor->ReadTemperature(sensor));
    TEST_ASSERT_EQUAL(5, mocked_temperature_sensor_get_celsius_fake.call_count);
}
void test_InternalTemperatureSensor_StateTransitions(void) {
    start();
    TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
    TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, sensor->base.GetState(&sensor->base));
    TEST_ASSERT_EQUAL(1, mocked_temperature_sensor_install_fake.call_count);
}
void test_InternalTemperatureSensor_ReadWithoutStart(void) {
    initialize();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -273.15f, sensor->ReadTemperature(sensor));
    TEST_ASSERT_EQUAL(0, mocked_temperature_sensor_get_celsius_fake.call_count);
}
void test_InternalTemperatureSensor_StabilityCheck(void) {
    start();
    float first=sensor->ReadTemperature(sensor);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, first, sensor->ReadTemperature(sensor));
}
