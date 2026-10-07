#include "overlay005/field_map_task_manager.h"

#include <nitro.h>
#include <string.h>

#include "field/field_system.h"
#include "field/field_system_sub2_t.h"
#include "overlay005/field_map_task_decl.h"
#include "overlay005/field_map_task_manager_decl.h"

#include "field_system.h"
#include "heap.h"
#include "sys_task.h"
#include "sys_task_manager.h"

struct FieldMapTaskManager {
    FieldSystem *fieldSystem;
    enum HeapID heapID;
    int maxTasks;
    FieldMapTask *tasks;
    SysTaskManager *renderTaskMan;
};

struct FieldMapTask {
    FieldMapTaskManager *taskMan;
    SysTask *updateTask;
    SysTask *renderTask;
    const FieldMapTaskTemplate *taskTemplate;
    void *data;
};

FieldMapTaskManager *FieldMapTaskManager_New(FieldSystem *fieldSystem, enum HeapID heapID, int maxTasks)
{
    FieldMapTaskManager *taskMan;
    u32 renderTaskManSize;

    taskMan = Heap_Alloc(heapID, sizeof(FieldMapTaskManager));

    taskMan->fieldSystem = fieldSystem;
    taskMan->heapID = heapID;
    taskMan->maxTasks = maxTasks;
    taskMan->tasks = Heap_Alloc(heapID, sizeof(FieldMapTask) * maxTasks);

    MI_CpuClear32(taskMan->tasks, sizeof(FieldMapTask) * maxTasks);

    renderTaskManSize = SysTaskManager_GetRequiredSize(maxTasks);

    taskMan->renderTaskMan = Heap_Alloc(heapID, renderTaskManSize);
    taskMan->renderTaskMan = SysTaskManager_Init(maxTasks, taskMan->renderTaskMan);

    return taskMan;
}

void FieldMapTaskManager_Free(FieldMapTaskManager *taskMan)
{
    int i;

    for (i = 0; i < taskMan->maxTasks; i++) {
        FieldMapTask_Remove(&taskMan->tasks[i]);
    }

    Heap_Free(taskMan->tasks);
    Heap_Free(taskMan->renderTaskMan);
    Heap_Free(taskMan);
}

void FieldMapTaskManager_Render(FieldMapTaskManager *taskMan)
{
    SysTaskManager_ExecuteTasks(taskMan->renderTaskMan);
}

static void FieldMapTask_RunUpdateCallback(SysTask *sysTask, void *param)
{
    FieldMapTask *task = param;

    if (FieldSystem_IsRunningFieldMapInner(task->taskMan->fieldSystem)) {
        if (task->taskTemplate->updateFunc) {
            task->taskTemplate->updateFunc(task, task->taskMan->fieldSystem, task->data);
        }
    }
}

static void FieldMapTask_RunRenderCallback(SysTask *sysTask, void *param)
{
    FieldMapTask *task = param;

    if (FieldSystem_IsRunningFieldMapInner(task->taskMan->fieldSystem)) {
        if (task->taskTemplate->renderFunc) {
            task->taskTemplate->renderFunc(task, task->taskMan->fieldSystem, task->data);
        }
    }
}

FieldMapTask *FieldMapTaskManager_Add(FieldMapTaskManager *taskMan, const FieldMapTaskTemplate *taskTemplate)
{
    int i;
    FieldMapTask *task;

    for (task = taskMan->tasks, i = 0; i < taskMan->maxTasks; task++, i++) {
        if (task->updateTask == NULL) {
            task->updateTask = SysTask_Start(FieldMapTask_RunUpdateCallback, task, taskTemplate->priority);
            task->renderTask = SysTaskManager_AddTask(taskMan->renderTaskMan, FieldMapTask_RunRenderCallback, task, taskTemplate->priority);
            task->taskMan = taskMan;
            task->taskTemplate = taskTemplate;

            GF_ASSERT(task->updateTask != NULL);
            GF_ASSERT(task->renderTask != NULL);

            if (taskTemplate->dataSize != 0) {
                task->data = Heap_Alloc(taskMan->heapID, taskTemplate->dataSize);
            }

            if (taskTemplate->initFunc) {
                taskTemplate->initFunc(task, taskMan->fieldSystem, task->data);
            }

            return task;
        }
    }

    GF_ASSERT(FALSE);
    return NULL;
}

void FieldMapTask_Remove(FieldMapTask *task)
{
    if (task->updateTask == NULL) {
        return;
    }

    if (task->taskTemplate->exitFunc) {
        task->taskTemplate->exitFunc(task, task->taskMan->fieldSystem, task->data);
    }

    if (task->taskTemplate->dataSize != 0) {
        Heap_Free(task->data);
    }

    SysTask_Done(task->updateTask);
    SysTask_Done(task->renderTask);

    MI_CpuClear32(task, sizeof(FieldMapTask));
}

void *FieldMapTask_GetData(FieldMapTask *task)
{
    return task->data;
}
