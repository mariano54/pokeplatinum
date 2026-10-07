#ifndef POKEPLATINUM_CAMERA_DISTANCE_ANIMATION_H
#define POKEPLATINUM_CAMERA_DISTANCE_ANIMATION_H

#include "constants/heap.h"

#include "field/field_system_decl.h"

#include "sys_task_manager.h"

// Smoothly moves the field camera closer or further away (e.g. zooming in on
// the player for HM cut-ins), then back to where it started.
enum CameraDistanceAnimationMode {
    CAMERA_DISTANCE_ANIMATION_IDLE = 0,
    CAMERA_DISTANCE_ANIMATION_OFFSET, // change the distance by `offset`
    CAMERA_DISTANCE_ANIMATION_RESTORE, // go back to the distance it had when created
};

SysTask *CameraDistanceAnimation_New(FieldSystem *fieldSystem, enum HeapID heapID);
BOOL CameraDistanceAnimation_IsDone(SysTask *task);
void CameraDistanceAnimation_Free(SysTask *task);
void CameraDistanceAnimation_Start(SysTask *task, int mode, fx32 offset, u32 frames);

#endif // POKEPLATINUM_CAMERA_DISTANCE_ANIMATION_H
