#include "overlay005/camera_distance_animation.h"

#include <nitro.h>
#include <string.h>

#include "field/field_system.h"

#include "camera.h"
#include "heap.h"
#include "sys_task.h"
#include "sys_task_manager.h"

enum CameraDistanceAnimationState {
    CAMERA_DISTANCE_ANIMATION_STATE_SETUP = 0,
    CAMERA_DISTANCE_ANIMATION_STATE_RUN,
    CAMERA_DISTANCE_ANIMATION_STATE_DONE,
};

typedef struct {
    enum HeapID heapID;
    int state;
    BOOL done;
    int mode;
    fx32 originalDistance;
    fx32 offset;
    fx32 targetDistance;
    u32 frames;
    fx32 distance;
    u32 frame;
    fx32 distancePerFrame;
    FieldSystem *fieldSystem;
    Camera *camera;
} CameraDistanceAnimation;

static void CameraDistanceAnimation_Task(SysTask *task, void *data);
static void CameraDistanceAnimation_ApplyDistance(CameraDistanceAnimation *anim);
static void CameraDistanceAnimation_SetupOffset(CameraDistanceAnimation *anim);
static void CameraDistanceAnimation_SetupRestore(CameraDistanceAnimation *anim);
static int CameraDistanceAnimation_Step(CameraDistanceAnimation *anim);

static void (*const sCameraDistanceAnimationUpdateFuncs[4])(CameraDistanceAnimation *);

SysTask *CameraDistanceAnimation_New(FieldSystem *fieldSystem, enum HeapID heapID)
{
    SysTask *task;
    CameraDistanceAnimation *anim = Heap_AllocAtEnd(heapID, (sizeof(CameraDistanceAnimation)));

    memset(anim, 0, (sizeof(CameraDistanceAnimation)));

    anim->heapID = heapID;
    anim->mode = CAMERA_DISTANCE_ANIMATION_IDLE;
    anim->fieldSystem = fieldSystem;
    anim->camera = fieldSystem->camera;
    anim->originalDistance = Camera_GetDistance(anim->camera);
    anim->distance = anim->originalDistance;

    task = SysTask_Start(CameraDistanceAnimation_Task, anim, 0xffff);
    return task;
}

BOOL CameraDistanceAnimation_IsDone(SysTask *task)
{
    CameraDistanceAnimation *anim = SysTask_GetParam(task);
    return anim->done;
}

void CameraDistanceAnimation_Free(SysTask *task)
{
    CameraDistanceAnimation *anim = SysTask_GetParam(task);

    Heap_Free(anim);
    SysTask_Done(task);
}

void CameraDistanceAnimation_Start(SysTask *task, int mode, fx32 offset, u32 frames)
{
    CameraDistanceAnimation *anim = SysTask_GetParam(task);

    anim->state = CAMERA_DISTANCE_ANIMATION_STATE_SETUP;
    anim->done = FALSE;
    anim->mode = mode;
    anim->offset = offset;
    anim->frames = frames;
    anim->frame = 0;
}

static void CameraDistanceAnimation_Task(SysTask *task, void *data)
{
    CameraDistanceAnimation *anim = data;
    sCameraDistanceAnimationUpdateFuncs[anim->mode](anim);
}

static void CameraDistanceAnimation_UpdateIdle(CameraDistanceAnimation *anim)
{
    anim->done = TRUE;
}

static void CameraDistanceAnimation_UpdateOffset(CameraDistanceAnimation *anim)
{
    switch (anim->state) {
    case CAMERA_DISTANCE_ANIMATION_STATE_SETUP:
        CameraDistanceAnimation_SetupOffset(anim);
        anim->state++;
    case CAMERA_DISTANCE_ANIMATION_STATE_RUN:
        if (CameraDistanceAnimation_Step(anim) == TRUE) {
            anim->state++;
            anim->done = TRUE;
        }

        CameraDistanceAnimation_ApplyDistance(anim);
    }
}

static void CameraDistanceAnimation_UpdateRestore(CameraDistanceAnimation *anim)
{
    switch (anim->state) {
    case CAMERA_DISTANCE_ANIMATION_STATE_SETUP:
        CameraDistanceAnimation_SetupRestore(anim);
        anim->state++;
    case CAMERA_DISTANCE_ANIMATION_STATE_RUN:
        if (CameraDistanceAnimation_Step(anim) == TRUE) {
            anim->state++;
            anim->done = TRUE;
        }

        CameraDistanceAnimation_ApplyDistance(anim);
    }
}

static void CameraDistanceAnimation_ApplyDistance(CameraDistanceAnimation *anim)
{
    Camera_SetDistance(anim->distance, anim->camera);
}

static void CameraDistanceAnimation_SetupOffset(CameraDistanceAnimation *anim)
{
    fx32 frames = anim->frames;

    anim->distancePerFrame = anim->offset / frames;
    anim->targetDistance = anim->distance + anim->offset;
}

static void CameraDistanceAnimation_SetupRestore(CameraDistanceAnimation *anim)
{
    fx32 frames = anim->frames;
    fx32 distanceLeft = anim->originalDistance - anim->distance;

    anim->distancePerFrame = distanceLeft / frames;
    anim->targetDistance = anim->originalDistance;
}

static int CameraDistanceAnimation_Step(CameraDistanceAnimation *anim)
{
    anim->distance += anim->distancePerFrame;
    anim->frame++;

    if (anim->frame >= anim->frames) {
        anim->frame = anim->frames;
        anim->distance = anim->targetDistance;
        return TRUE;
    }

    return FALSE;
}

static void (*const sCameraDistanceAnimationUpdateFuncs[4])(CameraDistanceAnimation *) = {
    [CAMERA_DISTANCE_ANIMATION_IDLE] = CameraDistanceAnimation_UpdateIdle,
    [CAMERA_DISTANCE_ANIMATION_OFFSET] = CameraDistanceAnimation_UpdateOffset,
    [CAMERA_DISTANCE_ANIMATION_RESTORE] = CameraDistanceAnimation_UpdateRestore,
};
