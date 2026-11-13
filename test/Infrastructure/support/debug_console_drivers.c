#include "Communication/DebugConsole/Drivers/UartConsoleIO.h"
#include "fff.h"
FAKE_VALUE_FUNC1(IConsoleIO *, UartConsoleIO_GetDriver, uart_port_t);
FAKE_VALUE_FUNC0(IConsoleIO *, StdioConsoleIO_GetDriver);

#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"
RotaryEncoderServiceImpl rotaryEncoderService;
FAKE_VALUE_FUNC2(esp_err_t, RotaryEncoder_Simulate, RotaryEncoderSimulation, uint16_t);

#include "test_mocks.h"
int test_console_log_level_override=-1;
esp_log_level_t esp_log_level_get(const char *tag) {
    (void)tag;
    return test_console_log_level_override>=0 ? (esp_log_level_t)test_console_log_level_override : mocked_esp_log_level_set_fake.arg1_val;
}
