#ifndef CONSOLE_COLORS_H
#define CONSOLE_COLORS_H

/**
 * @file ConsoleColors.h
 * @brief Merkezi ANSI renk ve stil kodlari tanimlari.
 *
 * Tum DebugConsole alt modulleri bu header'i kullanarak
 * renk/stil kodlarini tanimlar. DRY prensibine uygun olarak
 * tek bir noktadan yonetilir.
 */

// Renkler
#define CLI_COLOR_RESET   "\x1b[0m"
#define CLI_COLOR_RED     "\x1b[31m"
#define CLI_COLOR_GREEN   "\x1b[32m"
#define CLI_COLOR_YELLOW  "\x1b[33m"
#define CLI_COLOR_BLUE    "\x1b[34m"
#define CLI_COLOR_MAGENTA "\x1b[35m"
#define CLI_COLOR_CYAN    "\x1b[36m"
#define CLI_COLOR_WHITE   "\x1b[37m"

// Stiller
#define CLI_BOLD          "\x1b[1m"
#define CLI_DIM           "\x1b[2m"
#define CLI_UNDERLINE     "\x1b[4m"

// Ekran Kontrol
#define CLI_CLR_SCR       "\x1b[2J\x1b[H"
#define CLI_CLR_LINE      "\x1b[2K\r"

// Prompt
#define CLI_PROMPT        CLI_COLOR_GREEN "racool> " CLI_COLOR_RESET

#endif // CONSOLE_COLORS_H
