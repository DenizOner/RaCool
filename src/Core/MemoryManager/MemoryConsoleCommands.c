#include "Core/Localization/Localization.h"
#include "Core/MemoryManager/MemoryConsoleCommands.h"
#include "Core/MemoryManager/IMemoryManager.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static int do_mem_stats(int argc, char **argv, const LocalizationCatalog *catalog) {
  (void)argc; (void)argv;
  IMemoryManager *mgr = GetMemoryManager();
  if (!mgr) {
    LOC_PRINTF(catalog, LOC_CLI_MEMORY_MISSING,  "MemoryManager service not found!\n" );
    return 1;
  }

  size_t freeHeap = mgr->GetFreeHeap();
  MemoryManagerStats stats;
  if (MemoryManager_GetStats(&stats) != 0) { LOC_PRINTF(catalog, LOC_CLI_MEMORY_UNAVAILABLE, "Memory statistics unavailable\n"); return 1; }

  LOC_PRINTF(catalog, LOC_CLI_MEMORY_TITLE,  "=== Memory Manager Stats ===\n" );
  LOC_PRINTF(catalog, LOC_CLI_MEMORY_FREE, "Free Heap Size (IDF):  %u bytes (%.2f KB)\n", (unsigned int)freeHeap, (double)freeHeap / 1024.0);
  LOC_PRINTF(catalog, LOC_CLI_MEMORY_ALLOCATED, "Tracked Allocated:     %zu bytes (%.2f KB)\n", stats.current_bytes, (double)stats.current_bytes / 1024.0);
  LOC_PRINTF(catalog, LOC_CLI_MEMORY_PEAK, "Tracked Peak:          %zu bytes\n", stats.peak_bytes);
  LOC_PRINTF(catalog, LOC_CLI_MEMORY_BLOCKS, "Live Tracked Blocks:   %lu\n", (unsigned long)stats.live_blocks);
  LOC_PRINTF(catalog, LOC_CLI_MEMORY_FAILURES, "Failed Requests:       %lu\n", (unsigned long)stats.failed_requests);
  return 0;
}

static int do_mem_dump(int argc, char **argv, const LocalizationCatalog *catalog) {
  (void)argc; (void)argv;
  IMemoryManager *mgr = GetMemoryManager();
  if (!mgr) {
    LOC_PRINTF(catalog, LOC_CLI_MEMORY_MISSING,  "MemoryManager service not found!\n" );
    return 1;
  }

  MemoryAllocationRecord *dump_buffer = calloc(MEMORY_MANAGER_DUMP_LIMIT, sizeof(*dump_buffer));
  if (!dump_buffer) { LOC_PRINTF(catalog, LOC_CLI_MEMORY_SNAPSHOT_ALLOCATION, "Memory snapshot allocation failed\n"); return 1; }
  int count = MemoryManager_GetAllocRecords(dump_buffer, MEMORY_MANAGER_DUMP_LIMIT);
  if (count < 0) { free(dump_buffer); LOC_PRINTF(catalog, LOC_CLI_MEMORY_SNAPSHOT_UNAVAILABLE, "Memory snapshot unavailable\n"); return 1; }

  LOC_PRINTF(catalog, LOC_CLI_MEMORY_DUMP_TITLE,  "=== Active Memory Allocations (%d records) ===\n" , count);
  if (count == 0) {
    LOC_PRINTF(catalog, LOC_CLI_MEMORY_EMPTY, "No active tracked allocations.\n");
    free(dump_buffer);
    return 0;
  }

  printf("%-16s | %-10s | %s\n", Localization_Text(catalog, LOC_CLI_MEMORY_ADDRESS), Localization_Text(catalog, LOC_CLI_MEMORY_SIZE), Localization_Text(catalog, LOC_CLI_MEMORY_SOURCE));
  printf("-------------------------------------------------------------\n");
  for (int i = 0; i < count; i++) {
    // Get file basename for cleaner printout
    const char *fileName = dump_buffer[i].file;
    if (fileName) {
      const char *slash = strrchr(fileName, '/');
      if (!slash) slash = strrchr(fileName, '\\');
      if (slash) fileName = slash + 1;
    } else {
      fileName = Localization_Text(catalog, LOC_CLI_MEMORY_UNKNOWN_SOURCE);
    }

    printf("%p | %-10u | %s:%d\n",
           dump_buffer[i].ptr,
           (unsigned int)dump_buffer[i].size,
           fileName,
           dump_buffer[i].line);
  }
  free(dump_buffer);
  return 0;
}

static int cmd_mem(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  if (argc != 2 || !argv || !argv[0] || !argv[1] || !*argv[0] || !*argv[1]) {
    LOC_PRINTF(catalog, LOC_CLI_MEMORY_USAGE,  "Usage: MEM <STATS|DUMP>\n" );
    return 1;
  }

  if (strcasecmp(argv[1], "STATS") == 0) {
    return do_mem_stats(argc - 1, &argv[1], catalog);
  }
  if (strcasecmp(argv[1], "DUMP") == 0) {
    return do_mem_dump(argc - 1, &argv[1], catalog);
  }

  LOC_PRINTF(catalog, LOC_CLI_MEMORY_UNKNOWN,  "Unknown sub-command: %s\n" , argv[1]);
  return 1;
}

esp_err_t MemoryConsoleCommands_Register(IDebugConsole *console) {
  if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_ARG;

  static const esp_console_cmd_t mem_cmds[] = {
      {
          .command = "MEM",
          .help    = "Memory tracking and statistics inspection (STATS/DUMP)",
          .func    = &cmd_mem,
          .hint    = "<subcommand>",
      }
  };

  for (int i = 0; i < (int)(sizeof(mem_cmds) / sizeof(mem_cmds[0])); i++) {
    esp_err_t result = console->RegisterCommandWithCategory(console, &mem_cmds[i], CMD_CAT_DIAGNOSTIC);
    if (result != ESP_OK) return result;
  }
  return ESP_OK;
}
