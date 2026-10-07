#include "overlay005/warp_animation.h"

#include <nitro.h>
#include <nitro/os.h>
#include <string.h>

#include "constants/map_object.h"

#include "struct_decls/map_object.h"

#include "field/field_system.h"
#include "overlay005/ov5_021ECC20.h"

#include "heap.h"
#include "map_object.h"
#include "player_avatar.h"
#include "screen_fade.h"
#include "sound_playback.h"
#include "sys_task.h"
#include "sys_task_manager.h"

// Warp panels spin the player around while their sprite rises out of the
// screen (departing) or comes back down (arriving), over this many frames.
#define WARP_ANIMATION_FRAMES 20

enum WarpAnimationState {
    WARP_ANIMATION_STATE_START = 0,
    WARP_ANIMATION_STATE_MOVE,
    WARP_ANIMATION_STATE_WAIT_FOR_FADE,
};

typedef struct {
    FieldSystem *fieldSystem;
    BOOL *done;
    int state;
    int frame;
    int facingDir;
} WarpAnimation;

static void WarpAnimation_SpinPlayer(WarpAnimation *anim)
{
    switch (anim->facingDir) {
    case DIR_NORTH:
        anim->facingDir = DIR_WEST;
        break;
    case DIR_WEST:
        anim->facingDir = DIR_SOUTH;
        break;
    case DIR_SOUTH:
        anim->facingDir = DIR_EAST;
        break;
    case DIR_EAST:
        anim->facingDir = DIR_NORTH;
        break;
    }

    PlayerAvatar_TryFace(anim->fieldSystem->playerAvatar, anim->facingDir);
}

static void WarpAnimation_DepartTask(SysTask *task, void *data)
{
    WarpAnimation *anim = data;
    MapObject *mapObj = PlayerAvatar_GetMapObject(anim->fieldSystem->playerAvatar);
    VecFx32 spriteOffset;

    switch (anim->state) {
    case WARP_ANIMATION_STATE_START:
        anim->state = WARP_ANIMATION_STATE_MOVE;
        Sound_PlayEffect(SEQ_SE_DP_TELE2_sseq);
    case WARP_ANIMATION_STATE_MOVE:
        if (anim->frame % 2) {
            WarpAnimation_SpinPlayer(anim);
        }

        MapObject_GetSpritePosOffset(mapObj, &spriteOffset);
        spriteOffset.y = ((FX32_ONE * 2.2) + ((FX32_ONE / 2) * anim->frame)) * anim->frame;

        MapObject_SetSpritePosOffset(mapObj, &spriteOffset);
        anim->frame++;

        if (anim->frame == WARP_ANIMATION_FRAMES) {
            StartScreenFade(FADE_SUB_THEN_MAIN, FADE_TYPE_BRIGHTNESS_OUT, FADE_TYPE_BRIGHTNESS_OUT, COLOR_BLACK, 6, 1, HEAP_ID_FIELD1);
        } else if ((anim->frame > WARP_ANIMATION_FRAMES) && IsScreenFadeDone()) {
            *anim->done = TRUE;
            Heap_Free(anim);
            SysTask_Done(task);
        } else {
            break;
        }
    }
}

static void WarpAnimation_ArriveTask(SysTask *task, void *data)
{
    WarpAnimation *anim = data;
    MapObject *mapObj = PlayerAvatar_GetMapObject(anim->fieldSystem->playerAvatar);
    VecFx32 spriteOffset;
    int framesLeft;

    switch (anim->state) {
    case WARP_ANIMATION_STATE_START: {
        MapObject_SetPauseMovementOff(mapObj);
        MapObject_GetSpritePosOffset(mapObj, &spriteOffset);
        framesLeft = (WARP_ANIMATION_FRAMES - anim->frame);
        spriteOffset.y = ((FX32_ONE * 2.2) + ((FX32_ONE / 2) * framesLeft)) * framesLeft;
        MapObject_SetSpritePosOffset(mapObj, &spriteOffset);
        MapObject_Draw(mapObj);
    }

        Sound_PlayEffect(SEQ_SE_DP_TELE2_sseq);
        anim->state = WARP_ANIMATION_STATE_MOVE;
    case WARP_ANIMATION_STATE_MOVE:
        if (anim->frame % 2) {
            WarpAnimation_SpinPlayer(anim);
        }

        MapObject_GetSpritePosOffset(mapObj, &spriteOffset);
        framesLeft = (WARP_ANIMATION_FRAMES - anim->frame);
        spriteOffset.y = ((FX32_ONE * 2.2) + ((FX32_ONE / 2) * framesLeft)) * framesLeft;
        MapObject_SetSpritePosOffset(mapObj, &spriteOffset);
        anim->frame++;

        if (anim->frame == 2) {
            StartScreenFade(FADE_MAIN_THEN_SUB, FADE_TYPE_BRIGHTNESS_IN, FADE_TYPE_BRIGHTNESS_IN, COLOR_BLACK, 6, 1, HEAP_ID_FIELD1);
        }

        if (anim->frame > WARP_ANIMATION_FRAMES) {
            anim->state = WARP_ANIMATION_STATE_WAIT_FOR_FADE;
        }
        break;
    case WARP_ANIMATION_STATE_WAIT_FOR_FADE:
        if (IsScreenFadeDone()) {
            PlayerAvatar_TryFace(anim->fieldSystem->playerAvatar, DIR_SOUTH);
            *anim->done = TRUE;
            Heap_Free(anim);
            SysTask_Done(task);
        }
        break;
    }
}

void FieldSystem_StartWarpAnimation(FieldSystem *fieldSystem, BOOL departing, BOOL *done)
{
    WarpAnimation *anim = Heap_AllocAtEnd(HEAP_ID_FIELD1, sizeof(WarpAnimation));

    MI_CpuClear8(anim, sizeof(WarpAnimation));

    anim->fieldSystem = fieldSystem;
    anim->done = done;
    anim->facingDir = PlayerAvatar_GetFacingDir(fieldSystem->playerAvatar);

    if (departing) {
        SysTask_Start(WarpAnimation_DepartTask, anim, 100);
    } else {
        SysTask_Start(WarpAnimation_ArriveTask, anim, 100);
    }
}
