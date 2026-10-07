#include "overlay005/field_bottom_screen.h"

#include <nitro.h>
#include <string.h>

#include "field/field_system.h"
#include "overlay005/field_poketch.h"
#include "overlay056/ov56_022561C0.h"

#include "field_comm_manager.h"
#include "game_overlay.h"

FS_EXTERN_OVERLAY(overlay56);

typedef struct BottomScreenHandlers {
    // clang-format off
    void (* initFn)(FieldSystem *);
    BOOL (* isRunningDummyFn)(FieldSystem *);
    void (* endFn)(FieldSystem *);
    BOOL (* isDoneFn)(FieldSystem *);
    // clang-format on
} BottomScreenHandlers;

static void BottomScreen_InitPoketch(FieldSystem *fieldSystem);
static void BottomScreen_InitUnderground(FieldSystem *fieldSystem);
static void BottomScreen_InitUnionRoom(FieldSystem *fieldSystem);
static void BottomScreen_EndPoketch(FieldSystem *fieldSystem);
static void BottomScreen_EndUnderground(FieldSystem *fieldSystem);
static void BottomScreen_EndUnionRoom(FieldSystem *fieldSystem);
static BOOL BottomScreen_IsPoketchDone(FieldSystem *fieldSystem);
static BOOL BottomScreen_IsUndergroundDone(FieldSystem *fieldSystem);
static BOOL BottomScreen_IsUnionRoomDone(FieldSystem *fieldSystem);

static const BottomScreenHandlers sBottomScreenHandlers[] = {
    { BottomScreen_InitPoketch, NULL, BottomScreen_EndPoketch, BottomScreen_IsPoketchDone },
    { BottomScreen_InitUnderground, NULL, BottomScreen_EndUnderground, BottomScreen_IsUndergroundDone },
    { BottomScreen_InitUnionRoom, NULL, BottomScreen_EndUnionRoom, BottomScreen_IsUnionRoomDone },
    { FieldPoketch_InitUnavailableScreen, NULL, FieldPoketch_EndUnavailableScreen, FieldPoketch_IsUnavailableScreenDone }
};

static int FieldSystem_GetBottomScreenIndex(FieldSystem *fieldSystem)
{
    int fieldBottomScreen = fieldSystem->bottomScreen;

    GF_ASSERT(fieldBottomScreen != 0);
    GF_ASSERT(fieldBottomScreen < 5);

    return fieldBottomScreen - 1;
}

void FieldSystem_InitBottomScreen(FieldSystem *fieldSystem)
{
    sBottomScreenHandlers[FieldSystem_GetBottomScreenIndex(fieldSystem)].initFn(fieldSystem);
}

BOOL FieldSystem_IsBottomScreenRunningDummy(FieldSystem *fieldSystem)
{
    // clang-format off
    BOOL (* isRunningDummyFn)(FieldSystem *);
    // clang-format on

    isRunningDummyFn = sBottomScreenHandlers[FieldSystem_GetBottomScreenIndex(fieldSystem)].isRunningDummyFn;

    if (isRunningDummyFn == NULL) {
        return 1;
    }

    return isRunningDummyFn(fieldSystem);
}

void FieldSystem_EndBottomScreen(FieldSystem *fieldSystem)
{
    sBottomScreenHandlers[FieldSystem_GetBottomScreenIndex(fieldSystem)].endFn(fieldSystem);
}

BOOL FieldSystem_IsBottomScreenDone(FieldSystem *fieldSystem)
{
    return sBottomScreenHandlers[FieldSystem_GetBottomScreenIndex(fieldSystem)].isDoneFn(fieldSystem);
}

static void BottomScreen_InitPoketch(FieldSystem *fieldSystem)
{
    FieldPoketch_InitScreen(fieldSystem);
}

static void BottomScreen_InitUnderground(FieldSystem *fieldSystem)
{
    FieldCommManager_UnpauseUndergroundResources();
}

static void BottomScreen_InitUnionRoom(FieldSystem *fieldSystem)
{
    Overlay_LoadByID(FS_OVERLAY_ID(overlay56), 2);
    fieldSystem->unionRoomBottomScreen = ov56_02256410(fieldSystem);
}

static void BottomScreen_EndPoketch(FieldSystem *fieldSystem)
{
    FieldPoketch_EndScreen(fieldSystem);
}

static void BottomScreen_EndUnderground(FieldSystem *fieldSystem)
{
    FieldCommManager_PauseUndergroundResources();
}

static void BottomScreen_EndUnionRoom(FieldSystem *fieldSystem)
{
    ov56_02256468(fieldSystem->unionRoomBottomScreen);
    Overlay_UnloadByID(FS_OVERLAY_ID(overlay56));
}

static BOOL BottomScreen_IsPoketchDone(FieldSystem *fieldSystem)
{
    return FieldPoketch_IsScreenDone(fieldSystem);
}

static BOOL BottomScreen_IsUnionRoomDone(FieldSystem *fieldSystem)
{
    return 1;
}

static BOOL BottomScreen_IsUndergroundDone(FieldSystem *fieldSystem)
{
    return 1;
}
