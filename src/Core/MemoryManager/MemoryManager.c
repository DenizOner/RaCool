#include "Core/DI/IDIRegistry.h"
#include "Core/MemoryManager/IMemoryManager.h"
#include "esp_log.h"
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  void *ptr;
  size_t size;
  char *file;
  int line;
} AllocRecord;
typedef struct {
  IMemoryManager base;
  _Atomic(ServiceState) state;
  AllocRecord allocs[MEMORY_MANAGER_MAX_RECORDS];
  SemaphoreHandle_t mutex;
  size_t totalAllocated;
  size_t peakAllocated;
  atomic_uint failedRequests;
} MemoryManagerImpl;
static MemoryManagerImpl memManager;

/* The mutex and allocation table live for the process lifetime. Stop gates new
 * allocation, while owners may still release their blocks and inspect snapshots. */
static bool Mem_Lock(void) {
  return memManager.mutex && xSemaphoreTake(memManager.mutex,
      pdMS_TO_TICKS(MEMORY_MANAGER_LOCK_TIMEOUT_MS)) == pdTRUE;
}
static int32_t Mem_Start(IService *self) {
  if (!self || !Mem_Lock()) return -1;
  memManager.state = SERVICE_STATE_STARTED;
  xSemaphoreGive(memManager.mutex);
  return 0;
}
static int32_t Mem_Stop(IService *self) {
  if (!self || !Mem_Lock()) return -1;
  memManager.state = SERVICE_STATE_STOPPED;
  xSemaphoreGive(memManager.mutex);
  return 0;
}
static ServiceState Mem_GetState(IService *self) { (void)self; return memManager.state; }
static int32_t Mem_CheckHealth(IService *self) {
  return self && memManager.state == SERVICE_STATE_STARTED ? 0 : -1;
}
static void *Mem_AllocateTrack(size_t size, const char *file, int line) {
  if (!size || memManager.state != SERVICE_STATE_STARTED) goto failed;
  if (!file) file = "unknown";
  size_t length = strnlen(file, MEMORY_MANAGER_SOURCE_LIMIT);
  if (length == MEMORY_MANAGER_SOURCE_LIMIT) goto failed;
  const char *name = file;
  for (size_t i = 0; i < length; ++i)
    if (file[i] == '/' || file[i] == '\\') name = file + i + 1;
  length = strlen(name);
  if (length >= MEMORY_MANAGER_FILE_CAPACITY) goto failed;
  char *ownedFile = malloc(length + 1);
  if (!ownedFile) goto failed;
  memcpy(ownedFile, name, length + 1);
  void *ptr = malloc(size);
  if (!ptr) { free(ownedFile); goto failed; }
  bool tracked = false;
  if (Mem_Lock()) {
    if (memManager.state == SERVICE_STATE_STARTED && size <= SIZE_MAX - memManager.totalAllocated) {
      for (unsigned i = 0; i < MEMORY_MANAGER_MAX_RECORDS; ++i) {
        if (!memManager.allocs[i].ptr) {
          memManager.allocs[i] = (AllocRecord){ptr, size, ownedFile, line};
          memManager.totalAllocated += size;
          if (memManager.totalAllocated > memManager.peakAllocated)
            memManager.peakAllocated = memManager.totalAllocated;
          tracked = true;
          break;
        }
      }
    }
    xSemaphoreGive(memManager.mutex);
  }
  if (!tracked) { free(ptr); free(ownedFile); goto failed; }
  return ptr;
failed:
  atomic_fetch_add(&memManager.failedRequests, 1);
  return NULL;
}
static void Mem_FreeTrack(void *ptr, const char *file, int line) {
  (void)file; (void)line;
  if (!ptr) return;
  if (!Mem_Lock()) {
    ESP_LOGE("Mem", "Free rejected: allocation ownership lock unavailable");
    return;
  }
  char *ownedFile = NULL;
  bool found = false;
  for (unsigned i = 0; i < MEMORY_MANAGER_MAX_RECORDS; ++i) {
    if (memManager.allocs[i].ptr == ptr) {
      memManager.totalAllocated -= memManager.allocs[i].size;
      ownedFile = memManager.allocs[i].file;
      memManager.allocs[i] = (AllocRecord){0};
      found = true;
      break;
    }
  }
  xSemaphoreGive(memManager.mutex);
  if (!found) {
    ESP_LOGE("Mem", "Free rejected: block is not owned by this manager");
    return;
  }
  free(ownedFile);
  free(ptr);
}
static void *Mem_Allocate(size_t size) { return Mem_AllocateTrack(size, "unknown", 0); }
static void Mem_Free(void *ptr) { Mem_FreeTrack(ptr, "unknown", 0); }
static size_t Mem_GetFreeHeap(void) { return esp_get_free_heap_size(); }
static int32_t Mem_Initialize(IService *self, IDIRegistry *registry) {
  (void)registry;
  if (!self) return -1;
  if (memManager.mutex) return 0;
  SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  if (!mutex) return -1;
  memManager.mutex = mutex;
  memManager.state = SERVICE_STATE_INITIALIZED;
  return 0;
}
int MemoryManager_GetAllocRecords(MemoryAllocationRecord *dest, int max_count) {
  if (!dest || max_count <= 0 || !Mem_Lock()) return -1;
  int count = 0;
  for (unsigned i = 0; i < MEMORY_MANAGER_MAX_RECORDS && count < max_count; ++i) {
    const AllocRecord *record = &memManager.allocs[i];
    if (record->ptr) {
      dest[count].ptr = record->ptr;
      dest[count].size = record->size;
      dest[count].line = record->line;
      strcpy(dest[count].file, record->file);
      ++count;
    }
  }
  xSemaphoreGive(memManager.mutex);
  return count;
}
int32_t MemoryManager_GetTotalAllocated(size_t *total) {
  if (!total || !Mem_Lock()) return -1;
  *total = memManager.totalAllocated;
  xSemaphoreGive(memManager.mutex);
  return 0;
}
int32_t MemoryManager_GetStats(MemoryManagerStats *out) {
  if (!out || !Mem_Lock()) return -1;
  MemoryManagerStats snapshot = {.current_bytes = memManager.totalAllocated,
      .peak_bytes = memManager.peakAllocated,
      .failed_requests = atomic_load(&memManager.failedRequests)};
  for (unsigned i = 0; i < MEMORY_MANAGER_MAX_RECORDS; ++i)
    if (memManager.allocs[i].ptr) ++snapshot.live_blocks;
  xSemaphoreGive(memManager.mutex);
  *out = snapshot;
  return 0;
}
IMemoryManager *GetMemoryManager(void) {
  static bool initialized;
  if (!initialized) {
    memManager.base.base.Initialize = Mem_Initialize;
    memManager.base.base.Start = Mem_Start;
    memManager.base.base.Stop = Mem_Stop;
    memManager.base.base.GetState = Mem_GetState;
    memManager.base.base.CheckHealth = Mem_CheckHealth;
    memManager.base.Allocate = Mem_Allocate;
    memManager.base.Free = Mem_Free;
    memManager.base.GetFreeHeap = Mem_GetFreeHeap;
    memManager.base.AllocateTrack = Mem_AllocateTrack;
    memManager.base.FreeTrack = Mem_FreeTrack;
    initialized = true;
  }
  return &memManager.base;
}
