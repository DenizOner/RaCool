#include "Communication/DebugConsole/Core/DeveloperMode.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include "Communication/DebugConsole/Core/ConsolePrivacy.h"
#include <esp_console.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

static atomic_bool s_enabled = false;

void DeveloperMode_Reset(void) {
    atomic_store(&s_enabled, false);
}

void DeveloperMode_SetEnabled(bool enabled) {
    atomic_store(&s_enabled, enabled);
}

bool DeveloperMode_IsEnabled(void) {
    return atomic_load(&s_enabled);
}

static bool one_of(const char *word, const char *const *values, size_t count) {
    if (!word) return false;
    for (size_t i = 0; i < count; ++i)
        if (strcasecmp(word, values[i]) == 0) return true;
    return false;
}

#define IN_LIST(word, list) one_of((word), (list), sizeof(list) / sizeof((list)[0]))

bool DeveloperMode_IsUserRoot(const char *name) {
    /* The SDK also registers lowercase sys as a different internal handler. */
    if (name && strcmp(name, "sys") == 0) return false;
    static const char *const roots[] = {
        "HELP", "DEVELOPER", "LANGUAGE", "CLS", "SYSTEM", "SYS", "CLIMATE", "CC",
        "ZIGBEE", "ZB", "HARDWARE", "HW", "menu", "power", "UI", "UC",
        "rgb", "bled", "ir_temp", "int_temp", "RELAY",
        "builtin_led_boot", "builtin_led_list",
        "builtin_led_stop", "builtin_led_on", "builtin_led_off", "builtin_led_brightness",
        "rgbled_set", "rgbled_mode", "rgbled_brightness", "rgbled_anim"
    };
    return IN_LIST(name, roots);
}

bool DeveloperMode_IsVisibleUserRoot(const char *name) {
    static const char *const aliases[] = {"CC", "SYS", "ZB", "HW", "UC", "rgbled_set", "rgbled_mode",
        "rgbled_brightness", "rgbled_anim", "builtin_led_stop", "builtin_led_on", "builtin_led_off",
        "builtin_led_brightness"};
    return DeveloperMode_IsUserRoot(name) && strcmp(name, "help") != 0 && !IN_LIST(name, aliases);
}

/* Use the same tokenization as esp_console_run, without changing its input. */
static size_t split_command(const char *line, char *storage, char **words) {
    if (!line) return 0;
    size_t length = strnlen(line, DEBUG_CONSOLE_LINE_BUFFER_SIZE);
    if (!length || length >= DEBUG_CONSOLE_LINE_BUFFER_SIZE) return 0;
    memcpy(storage, line, length + 1);
    return esp_console_split_argv(storage, words, 4);
}

static bool user_words(const char *root, const char *sub, const char *third) {
    if (!DeveloperMode_IsUserRoot(root)) return false;
    if (!strcasecmp(root, "DEVELOPER")) {
        static const char *const allowed[] = {"", "on", "off", "status", "--machine"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "SYSTEM") || !strcasecmp(root, "SYS")) {
        if (!strcasecmp(sub, "CONFIG")) return !strcasecmp(third, "SET") || !strcasecmp(third, "XY");
        static const char *const allowed[] = {"RESET", "FACTORY_RESET", "VERSION"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "CLIMATE") || !strcasecmp(root, "CC")) {
        static const char *const allowed[] = {"UNIT", "STATUS", "INFO", "SET", "MODE", "SOURCE", "DELTA", "EXTERNAL", "CAL", "SAVE", "RESET"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "ZIGBEE") || !strcasecmp(root, "ZB")) {
        static const char *const allowed[] = {"STEER", "TX", "REPORT"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "HARDWARE") || !strcasecmp(root, "HW")) {
        if (!strcasecmp(sub, "TEMP")) return !third[0] || !strcmp(third, "--machine");
        static const char *const allowed[] = {"RELAY", "RGB", "LED"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "menu")) return !strcasecmp(sub, "auto-exit");
    if (!strcasecmp(root, "power")) {
        static const char *const allowed[] = {"", "mode", "sleep", "restart"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "UI") || !strcasecmp(root, "UC")) return !strcasecmp(sub, "WAKE");
    if (!strcasecmp(root, "ir_temp")) {
        static const char *const allowed[] = {"", "trigger"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "int_temp")) {
        static const char *const allowed[] = {"", "read"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "bled")) {
        static const char *const allowed[] = {"", "on", "off", "anim", "brightness"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "rgb")) {
        static const char *const allowed[] = {"", "status", "list", "set", "brightness", "anim", "off"};
        if (!strcasecmp(sub, "status") && !strcmp(third, "--machine")) return false;
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "LANGUAGE")) {
        static const char *const allowed[] = {"", "--machine", "list", "set", "config"};
        return IN_LIST(sub, allowed);
    }
    if (!strcasecmp(root, "RELAY")) {
        static const char *const allowed[] = {"ON", "OFF", "TOGGLE"};
        return IN_LIST(sub, allowed);
    }
    return true;
}

bool DeveloperMode_IsUserLine(const char *line) {
    char storage[DEBUG_CONSOLE_LINE_BUFFER_SIZE] = {0};
    char *words[4] = {0};
    size_t count = split_command(line, storage, words);
    bool result = count && words[0] && words[0][0] && user_words(words[0],
        count > 1 ? words[1] : "", count > 2 ? words[2] : "");
    ConsolePrivacy_Clear(storage, sizeof(storage));
    return result;
}

const char *DeveloperMode_UserUsage(const char *line) {
    char storage[DEBUG_CONSOLE_LINE_BUFFER_SIZE] = {0};
    char *words[4] = {0};
    const char *result = NULL;
    if (split_command(line, storage, words) == 1 && words[0] && words[0][0]) {
        const char *root = words[0];
        if (!strcmp(root, "SYS") || !strcmp(root, "SYSTEM")) result = "SYSTEM RESET|FACTORY_RESET|VERSION|CONFIG";
        else if (!strcmp(root, "CLIMATE") || !strcmp(root, "CC")) result = "CLIMATE UNIT|STATUS|INFO|SET|MODE|SOURCE|DELTA|EXTERNAL|CAL|SAVE|RESET";
        else if (!strcmp(root, "ZIGBEE") || !strcmp(root, "ZB")) result = "ZIGBEE STEER|TX|REPORT";
        else if (!strcmp(root, "HARDWARE") || !strcmp(root, "HW")) result = "HW TEMP|RELAY|RGB|LED";
        else if (!strcmp(root, "RELAY")) result = "RELAY ON|OFF|TOGGLE";
        else if (!strcmp(root, "UI") || !strcmp(root, "UC")) result = "UI WAKE";
        else if (!strcmp(root, "menu")) result = "menu auto-exit on|off|status";
    }
    ConsolePrivacy_Clear(storage, sizeof(storage));
    return result;
}
