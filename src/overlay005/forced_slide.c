#include "overlay005/forced_slide.h"

#include <nitro.h>
#include <string.h>

#include "constants/map_object.h"
#include "constants/player_avatar.h"
#include "generated/movement_actions.h"

#include "struct_decls/map_object.h"

#include "field/field_system.h"

#include "field_task.h"
#include "heap.h"
#include "map_object.h"
#include "map_tile_behavior.h"
#include "player_avatar.h"
#include "player_move.h"
#include "sound_playback.h"
#include "unk_020655F4.h"

// Slide tiles push the player one tile at a time, spinning them around on the
// way, until they reach a tile they can't move onto.
enum ForcedSlideState {
    FORCED_SLIDE_STATE_START = 0,
    FORCED_SLIDE_STATE_STEP,
    FORCED_SLIDE_STATE_SPIN,
};

#define FORCED_SLIDE_STEP_FRAMES 7

typedef struct {
    int dir;
    int stepTimer;
    int state;
    FieldSystem *fieldSystem;
    PlayerAvatar *playerAvatar;
    MapObject *unused;
} ForcedSlide;

static void ForcedSlide_Start(FieldSystem *fieldSystem, PlayerAvatar *playerAvatar, int dir);
static BOOL ForcedSlide_Task(FieldTask *task);
static void *ForcedSlide_Alloc(int size);
static void ForcedSlide_Free(void *slide);

int ForcedSlide_TryStart(FieldSystem *fieldSystem, PlayerAvatar *playerAvatar, int playerDir)
{
    MapObject *mapObj = PlayerAvatar_GetMapObject(playerAvatar);
    u8 tileBehavior = MapObject_GetCurrTileBehavior(mapObj);
    int dir;

    if (TileBehavior_IsSlideEastward(tileBehavior) == TRUE) {
        dir = DIR_EAST;
    } else if (TileBehavior_IsSlideWestward(tileBehavior) == TRUE) {
        dir = DIR_WEST;
    } else if (TileBehavior_IsSlideNorthward(tileBehavior) == TRUE) {
        dir = DIR_NORTH;
    } else if (TileBehavior_IsSlideSouthward(tileBehavior) == TRUE) {
        dir = DIR_SOUTH;
    } else {
        return FALSE;
    }

    ForcedSlide_Start(fieldSystem, playerAvatar, dir);
    return TRUE;
}

static void ForcedSlide_Start(FieldSystem *fieldSystem, PlayerAvatar *playerAvatar, int dir)
{
    ForcedSlide *slide = ForcedSlide_Alloc(sizeof(ForcedSlide));

    slide->fieldSystem = fieldSystem;
    slide->playerAvatar = playerAvatar;
    slide->dir = dir;

    Sound_PlayEffect(SEQ_SE_DP_F209_sseq);
    FieldSystem_CreateTask(fieldSystem, ForcedSlide_Task, slide);
}

static int RotateDirCounterclockwise(int dir)
{
    switch (dir) {
    case DIR_NORTH:
        return DIR_WEST;
    case DIR_WEST:
        return DIR_SOUTH;
    case DIR_SOUTH:
        return DIR_EAST;
    case DIR_EAST:
        return DIR_NORTH;
    }

    return DIR_NORTH;
}

static BOOL ForcedSlide_Task(FieldTask *task)
{
    ForcedSlide *slide = FieldTask_GetEnv(task);
    MapObject *mapObj = PlayerAvatar_GetMapObject(slide->playerAvatar);
    u8 tileBehavior = MapObject_GetCurrTileBehavior(mapObj);

    switch (slide->state) {
    case FORCED_SLIDE_STATE_START:
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_PAUSE_ANIMATION);
        slide->state++;
        break;
    case FORCED_SLIDE_STATE_STEP:
        if (PlayerAvatar_IsMapObjectAnimationSet(slide->playerAvatar)) {
            int movementAction = MOVEMENT_ACTION_WALK_NORMAL_NORTH;

            movementAction = MovementAction_TurnActionTowardsDir(slide->dir, movementAction);
            PlayerAvatar_SetMapObjMovement(slide->playerAvatar, movementAction, 1);
            PlayerAvatar_TryFace(slide->playerAvatar, slide->dir);
            slide->state++;
            slide->stepTimer = FORCED_SLIDE_STEP_FRAMES;
        }
        break;
    case FORCED_SLIDE_STATE_SPIN:
        switch (slide->stepTimer) {
        case 6:
        case 4:
        case 2:
            slide->dir = RotateDirCounterclockwise(slide->dir);
            PlayerAvatar_TryFace(slide->playerAvatar, slide->dir);
            break;
        default:
            break;
        }

        slide->stepTimer--;

        if (slide->stepTimer == 0) {
            if (TileBehavior_IsSlideEastward(tileBehavior) == TRUE) {
                slide->dir = DIR_EAST;
            } else if (TileBehavior_IsSlideWestward(tileBehavior) == TRUE) {
                slide->dir = DIR_WEST;
            } else if (TileBehavior_IsSlideNorthward(tileBehavior) == TRUE) {
                slide->dir = DIR_NORTH;
            } else if (TileBehavior_IsSlideSouthward(tileBehavior) == TRUE) {
                slide->dir = DIR_SOUTH;
            } else {
                slide->dir = RotateDirCounterclockwise(slide->dir);
            }

            {
                u32 collision = PlayerAvatar_CheckCollision(slide->playerAvatar, mapObj, slide->dir);

                if (collision == PLAYER_COLLISION_NONE) {
                    slide->state = FORCED_SLIDE_STATE_STEP;
                } else {
                    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_LOCK_DIR);
                    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_PAUSE_ANIMATION);
                    PlayerAvatar_TryFace(slide->playerAvatar, slide->dir);
                    ForcedSlide_Free(slide);
                    Sound_StopEffect(SEQ_SE_DP_F209_sseq, 0);
                    return TRUE;
                }
            }
        }
        break;
    }

    return FALSE;
}

static void *ForcedSlide_Alloc(int size)
{
    void *slide = Heap_AllocAtEnd(HEAP_ID_FIELD1, size);

    GF_ASSERT(slide != NULL);
    memset(slide, 0, size);

    return slide;
}

static void ForcedSlide_Free(void *slide)
{
    Heap_FreeExplicit(HEAP_ID_FIELD1, slide);
}
