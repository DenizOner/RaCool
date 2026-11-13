#include "Communication/SerialMonitor/ISerialMonitor.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_console.h"
#include <stdio.h>
#include <string.h>

extern int32_t SerialMonitor_DiagnosticHealthHold(void);
static bool owned;
static int command(int argc, char **argv) {
    if (argc != 2 || !argv || !argv[0] || !argv[1]) return -1;
    ISerialMonitor *monitor = SerialMonitor_GetModule();
    if (!monitor || !monitor->base.GetState || !monitor->base.Start) return -1;
    if (!strcmp(argv[1], "hold")) {
        if (owned || SerialMonitor_DiagnosticHealthHold() != 0) return -1;
        owned = true;
    } else if (!strcmp(argv[1], "restore")) {
        if (!owned || monitor->base.Start(&monitor->base) != 0) return -1;
        owned = false;
    } else if (strcmp(argv[1], "status")) return -1;
    printf("HEALTH_REPORT state=%d owned=%d\n", monitor->base.GetState(&monitor->base), owned);
    return 0;
}
esp_err_t HealthReportDiagnostic_Register(IDebugConsole *console) {
    static const esp_console_cmd_t cmd = {.command="health_report", .help="Temporary real service health test", .func=command};
    return console && console->RegisterCommand ? console->RegisterCommand(console, &cmd) : ESP_ERR_INVALID_STATE;
}
