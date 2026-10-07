#include "overlay005/bike_slope.h"

#include <nitro.h>
#include <string.h>

#include "field/field_system.h"
#include "field/field_system_sub2_t.h"
#include "overlay005/entrance_animation.h"
#include "overlay005/field_map_task_decl.h"
#include "overlay005/field_map_task_manager.h"

#include "map_tile_behavior.h"
#include "player_avatar.h"
#include "terrain_collision_manager.h"

// Index of the muddy slope prop animation played when the player steps on
// either end of a bike slope.
enum BikeSlopeAnimation {
    BIKE_SLOPE_ANIMATION_BOTTOM = 0,
    BIKE_SLOPE_ANIMATION_TOP,
};

typedef struct {
    int lastPlayerX;
    int lastPlayerZ;
} BikeSlopeTask;

static void BikeSlopeTask_Init(FieldMapTask *task, FieldSystem *fieldSystem, void *data)
{
    BikeSlopeTask *slopeTask = (BikeSlopeTask *)data;

    slopeTask->lastPlayerX = 0;
    slopeTask->lastPlayerZ = 0;
}

static void BikeSlopeTask_Exit(FieldMapTask *task, FieldSystem *fieldSystem, void *data)
{
    return;
}

static void BikeSlopeTask_Update(FieldMapTask *task, FieldSystem *fieldSystem, void *data)
{
    u8 tileBehavior;
    int playerX, playerZ;
    u8 animation;
    BikeSlopeTask *slopeTask = (BikeSlopeTask *)data;

    playerX = PlayerAvatar_GetXPos(fieldSystem->playerAvatar);
    playerZ = PlayerAvatar_GetZPos(fieldSystem->playerAvatar);

    if ((slopeTask->lastPlayerX == playerX) && (slopeTask->lastPlayerZ == playerZ)) {
        return;
    }

    slopeTask->lastPlayerX = playerX;
    slopeTask->lastPlayerZ = playerZ;

    tileBehavior = TerrainCollisionManager_GetTileBehavior(fieldSystem, playerX, playerZ);

    if (TileBehavior_IsBikeSlopeBottom(tileBehavior)) {
        animation = BIKE_SLOPE_ANIMATION_BOTTOM;
    } else if (TileBehavior_IsBikeSlopeTop(tileBehavior)) {
        animation = BIKE_SLOPE_ANIMATION_TOP;
    } else {
        return;
    }

    BikeSlope_PlayPropAnimation(playerX, playerZ, animation, fieldSystem);
}

static const FieldMapTaskTemplate sBikeSlopeTaskTemplate = {
    .priority = 2,
    .dataSize = sizeof(BikeSlopeTask),
    .initFunc = BikeSlopeTask_Init,
    .exitFunc = BikeSlopeTask_Exit,
    .updateFunc = BikeSlopeTask_Update,
    .renderFunc = NULL,
};

void BikeSlope_StartTask(FieldSystem *fieldSystem)
{
    FieldMapTaskManager_Add(fieldSystem->fieldMapSubsystems->fieldMapTaskMan, &sBikeSlopeTaskTemplate);
}
