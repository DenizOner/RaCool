// Discoverable PlatformIO entry for the real RGB service and LEDC hardware suite.
#include "../Hardware/RGBLED/test_RGBLED.c"
#include "../../src/Hardware/LEDAnimations/LEDTransition.c"
#include "../../src/Hardware/LEDAnimations/RainbowFlowAnim.c"
#include "../../src/Hardware/LEDAnimations/LEDAnimationInstances.c"
#include "../../src/Hardware/LEDAnimations/BlinkRedAnim.c"
#include "../../src/Hardware/LEDAnimations/BlinkWhiteAnim.c"
#include "../../src/Hardware/LEDAnimations/BreathingGreenAnim.c"
#include "../../src/Hardware/LEDAnimations/BreathingRedAnim.c"
#include "../../src/Hardware/LEDAnimations/DoubleBlinkWhiteAnim.c"
#include "../../src/Hardware/LEDAnimations/FadeBlueAnim.c"
#include "../../src/Hardware/LEDAnimations/HeartbeatAnim.c"
#include "../../src/Hardware/LEDAnimations/PoliceStrobeAnim.c"
#include "../../src/Hardware/LEDAnimations/SolidBlueAnim.c"
#include "../../src/Hardware/LEDAnimations/SolidGreenAnim.c"
#include "../../src/Hardware/LEDAnimations/SolidRedAnim.c"
#include "../../src/Hardware/LEDAnimations/SolidWhiteAnim.c"
#include "../../src/Hardware/LEDAnimations/SOSRedAnim.c"
#include "../../src/Hardware/LEDAnimations/StrobeWhiteAnim.c"


// Isolate the hardware service from the application startup and Zigbee network.
// Inject only configuration; production worker, timers and LEDC calls are real.
static atomic_uchar target_rgb_brightness = 255;
void TargetRGB_SetBrightnessConfig(uint8_t brightness) { atomic_store(&target_rgb_brightness, brightness); }
static esp_err_t target_rgb_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self;
    memset(out, 0, sizeof(*out));
    out->system.system_enabled = true;
    out->hardware.ext_led_brightness = atomic_load(&target_rgb_brightness);
    out->hardware.ext_led_mode = EXT_LED_MODE_TEMPERATURE;
    return ESP_OK;
}
static IConfigurationManager target_rgb_config = {.GetSnapshot=target_rgb_snapshot};
IService *ServiceLocator_GetService(const char *name) {
    return strcmp(name, SVC_NAME_CONFIGURATION_MANAGER) == 0 ? &target_rgb_config.base : NULL;
}
IEventBus *ServiceLocator_GetEventBus(void) { return NULL; }
IConfigurationManager *ConfigurationManager_GetInstance(void) { return &target_rgb_config; }
bool BootAnimation_IsRunning(void) { return false; }

#include "../../src/Management/Watchdog/WatchdogWorker.c"

#include "../../src/Hardware/LEDAnimations/LEDAnimationPatterns.c"
#include "../../src/Hardware/LEDAnimations/GenericPatternsAnim.c"
