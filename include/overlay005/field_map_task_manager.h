#ifndef POKEPLATINUM_FIELD_MAP_TASK_MANAGER_H
#define POKEPLATINUM_FIELD_MAP_TASK_MANAGER_H

#include "constants/heap.h"

#include "field/field_system_decl.h"
#include "overlay005/field_map_task_decl.h"
#include "overlay005/field_map_task_manager_decl.h"

typedef void (*FieldMapTaskFunc)(FieldMapTask *, FieldSystem *, void *);

// Describes a task that is owned by the field map and freed along with it.
// Every callback is optional and receives the task's data buffer (dataSize
// bytes, allocated by the manager). updateFunc runs as a regular SysTask and
// renderFunc during the field map's 3D render pass; both are skipped while the
// field map isn't running.
typedef struct FieldMapTaskTemplate {
    u32 priority;
    u16 dataSize;
    FieldMapTaskFunc initFunc;
    FieldMapTaskFunc exitFunc;
    FieldMapTaskFunc updateFunc;
    FieldMapTaskFunc renderFunc;
} FieldMapTaskTemplate;

FieldMapTaskManager *FieldMapTaskManager_New(FieldSystem *fieldSystem, enum HeapID heapID, int maxTasks);
void FieldMapTaskManager_Free(FieldMapTaskManager *taskMan);
void FieldMapTaskManager_Render(FieldMapTaskManager *taskMan);
FieldMapTask *FieldMapTaskManager_Add(FieldMapTaskManager *taskMan, const FieldMapTaskTemplate *taskTemplate);
void FieldMapTask_Remove(FieldMapTask *task);
void *FieldMapTask_GetData(FieldMapTask *task);

#endif // POKEPLATINUM_FIELD_MAP_TASK_MANAGER_H
