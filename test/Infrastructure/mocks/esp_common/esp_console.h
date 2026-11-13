#ifndef ESP_CONSOLE_H
#define ESP_CONSOLE_H
#include <stddef.h>
typedef struct { size_t max_cmdline_length; size_t max_cmdline_args; int hint_color; int hint_bold; } esp_console_config_t;
#define ESP_CONSOLE_CONFIG_DEFAULT() ((esp_console_config_t){.max_cmdline_length=256,.max_cmdline_args=8})
size_t esp_console_split_argv(char *line, char **argv, size_t argv_size);
#endif // ESP_CONSOLE_H
