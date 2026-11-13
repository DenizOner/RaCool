#ifndef ESP_ROM_GPIO_H
#define ESP_ROM_GPIO_H

#include <stdint.h>
#include <stdbool.h>

static inline void esp_rom_gpio_pad_select_gpio(uint32_t gpio_num) {
    (void)gpio_num;
}

static inline void esp_rom_gpio_connect_out_signal(uint32_t gpio_num, uint32_t signal_idx, bool out_inv, bool oen_inv) {
    (void)gpio_num;
    (void)signal_idx;
    (void)out_inv;
    (void)oen_inv;
}

#endif // ESP_ROM_GPIO_H
