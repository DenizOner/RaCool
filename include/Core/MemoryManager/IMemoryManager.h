#ifndef IMEMORYMANAGER_H
#define IMEMORYMANAGER_H

#include "Core/IService.h"
#include "Core/MemoryManager/MemoryManagerConfig.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct IMemoryManager {
  IService base;
  void *(*Allocate)(size_t size);
  void (*Free)(void *ptr);
  size_t (*GetFreeHeap)(void);
  
  // Advanced tracking capabilities
  void *(*AllocateTrack)(size_t size, const char *file, int line);
  void (*FreeTrack)(void *ptr, const char *file, int line);
} IMemoryManager;

/* Factory initialization precedes concurrent consumers; methods are task-only.
 * Allocate owns a tracked block. Free accepts only a block issued by this manager.
 * Stop retains live blocks; Initialize never resets their ownership. */
typedef struct {
  void *ptr;
  size_t size;
  char file[MEMORY_MANAGER_FILE_CAPACITY];
  int line;
} MemoryAllocationRecord;
int MemoryManager_GetAllocRecords(MemoryAllocationRecord *dest, int max_count);
int32_t MemoryManager_GetTotalAllocated(size_t *total);
/* Process-lifetime tracked-block statistics, not global heap usage. Failed
 * requests include invalid inputs, stopped admission, allocation and lock errors.
 * The uint32 failure counter wraps modulo 2^32. Failed snapshots preserve output. */
typedef struct {
  size_t current_bytes;
  size_t peak_bytes;
  uint32_t live_blocks;
  uint32_t failed_requests;
} MemoryManagerStats;
int32_t MemoryManager_GetStats(MemoryManagerStats *out);
IMemoryManager *GetMemoryManager(void);

#define MEM_ALLOC(size) GetMemoryManager()->AllocateTrack(size, __FILE__, __LINE__)
#define MEM_FREE(ptr) GetMemoryManager()->FreeTrack(ptr, __FILE__, __LINE__)

#endif // IMEMORYMANAGER_H

