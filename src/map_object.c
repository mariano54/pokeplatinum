#include "map_object.h"

#include <nitro.h>
#include <string.h>

#include "generated/movement_types.h"
#include "generated/object_events_gfx.h"

#include "struct_decls/struct_02061830_sub1_decl.h"
#include "struct_defs/struct_020EDF0C.h"

#include "field/field_system.h"
#include "functypes/funcptr_020EDF0C.h"
#include "functypes/funcptr_020EDF0C_1.h"
#include "functypes/funcptr_020EDF0C_2.h"
#include "overlay005/object_event_gfx_data.h"
#include "overlay005/ov5_021ECC20.h"
#include "overlay005/ov5_021ECE40.h"
#include "overlay005/struct_ov5_021ED0A4.h"

#include "berry_patch_graphics.h"
#include "heap.h"
#include "map_header_data.h"
#include "map_object_move.h"
#include "narc.h"
#include "script_manager.h"
#include "sys_task.h"
#include "sys_task_manager.h"
#include "unk_020655F4.h"
#include "unk_020EDBAC.h"

typedef struct MapObjectManager {
    u32 status;
    int maxObjects;
    int objectCnt;
    int taskBasePriority;
    int unused_10;
    NARC *narc;
    UnkStruct_ov5_021ED0A4 renderManager;
    UnkStruct_02061830_sub1 *unused_120;
    MapObject *mapObj;
    FieldSystem *fieldSystem;
} MapObjectManager;

/*
 * Map objects normally belong to the map whose object events spawned them, but
 * an object event without a script (script == 0xFFFF) is a reference to an
 * object owned by another map, with the owner's map header ID stored in its
 * hidden flag field. This lets an object stay loaded when the player walks into
 * a map that references it instead of being deleted and spawned again. While an
 * object is attached to a map other than its owner, it is "borrowed":
 * MAP_OBJ_STATUS_25 is set and its flag field holds the owner's map header ID
 * instead of a hidden flag.
 */
typedef struct MapObject {
    u32 status;
    u32 extraStatus;
    u32 localID;
    enum MapHeaderID mapHeaderID;
    u32 graphicsID;
    u32 movementType;
    u32 trainerType;
    u32 flag;
    u32 script;
    int initialDir;
    int facingDir;
    int movingDir;
    int prevFacingDir;
    int prevMovingDir;
    int data[3];
    int movementRangeX;
    int movementRangeZ;
    int xInitial;
    int yInitial;
    int zInitial;
    int xPrev;
    int yPrev;
    int zPrev;
    int x;
    int y;
    int z;
    VecFx32 pos;
    VecFx32 spriteJumpOffset;
    VecFx32 spritePosOffset;
    VecFx32 spriteTerrainOffset;
    u32 spriteAnimCode; // Selects the sprite animation the renderer plays, see MAP_OBJ_UNK_A0_*
    enum MovementAction movementAction;
    int movementStep;
    u16 currTileBehavior;
    u16 prevTileBehavior;
    SysTask *task;
    const MapObjectManager *mapObjMan;
    UnkFuncPtr_020EDF0C moveInitFunc;
    UnkFuncPtr_020EDF0C_1 moveFunc;
    UnkFuncPtr_020EDF0C_2 moveDeleteFunc;
    MapObjectDrawInitFunc drawInitFunc;
    MapObjectDrawFunc drawFunc;
    MapObjectDrawDeleteFunc drawDeleteFunc;
    MapObjectDrawPauseFunc drawPauseFunc;
    MapObjectDrawResumeFunc drawResumeFunc;
    u8 movementTypeData[16];
    u8 trainerTypeData[16];
    u8 movementData[16];
    u8 drawData[32];
} MapObject;

typedef struct ObjectEventLoader {
    int mapHeaderID;
    int numObjectEvents;
    int index;
    const MapObjectManager *mapObjMan;
    ObjectEvent *objectEvents;
} ObjectEventLoader;

static MapObjectManager *MapObjectMan_Alloc(int maxObjs);
static void MapObject_Save(FieldSystem *fieldSystem, MapObject *mapObj, MapObjectSave *mapObjSave);
static void MapObject_LoadSave(MapObject *mapObj, MapObjectSave *mapObjSave);
static void MapObject_InitAfterLoad(const MapObjectManager *mapObjMan, MapObject *mapObj);
static void MapObject_ResetStatusAfterLoad(MapObject *mapObj);
static void MapObject_InitPosAfterLoad(MapObject *mapObj);
static void ObjectEventLoader_AddMapObjects(ObjectEventLoader *loader);
static MapObject *MapObjectMan_GetFreeMapObject(const MapObjectManager *mapObjMan);
static MapObject *MapObjectMan_FindBorrowedObject(const MapObjectManager *mapObjMan, int localID, int ownerMapHeaderID);
static void MapObjectMan_AddMoveTask(const MapObjectManager *mapObjMan, MapObject *mapObj);
static void MapObject_InitFromObjectEvent(MapObject *mapObj, const ObjectEvent *objectEvent, FieldSystem *fieldSystem);
static void MapObject_InitPosFromObjectEvent(MapObject *mapObj, const ObjectEvent *objectEvent);
static void MapObject_InitState(MapObject *mapObj, const MapObjectManager *mapObjMan);
static void MapObject_SetMoveFuncs(MapObject *mapObj);
static void MapObject_SetDrawFuncs(MapObject *mapObj);
static void MapObject_Clear(MapObject *mapObj);
static int MapObject_FindInObjectEvents(const MapObject *mapObj, int mapHeaderID, int objEventCount, const ObjectEvent *objectEvent);
static MapObject *MapObjectMan_FindObjectInMap(const MapObjectManager *mapObjMan, int localID, int mapHeaderID);
static void MapObject_RestartFieldEffects(MapObject *mapObj);
static void MapObject_ClearFieldEffectFlags(MapObject *mapObj);
static void MapObject_OnDrawPausedNoOp(MapObject *mapObj);
static int MapObject_GetFieldSystemGraphicsID(FieldSystem *fieldSystem, int graphicsID);
static void MapObject_UpdateHeightIfPending(MapObject *mapObj);
static void MapObject_SetupMovement(MapObject *mapObj);
static void MapObject_InitDraw(MapObject *mapObj);
static void MapObject_ReturnToOwnerMap(MapObject *mapObj, const ObjectEvent *objectEvent, enum MapHeaderID mapHeaderID);
static void MapObject_LendToMap(MapObject *mapObj, enum MapHeaderID mapHeaderID, const ObjectEvent *objectEvent);
static void MapObjectTask_Move(SysTask *task, void *_mapObject);
static void MapObjectTask_Draw(MapObject *mapObj);
static MapObjectManager *MapObjectMan_Deconst(const MapObjectManager *mapObjMan);
static void MapObjectMan_IncObjectCount(MapObjectManager *mapObjMan);
static void MapObjectMan_DecObjectCount(MapObjectManager *mapObjMan);
static MapObject *MapObjectMan_GetMapObjectStatic(const MapObjectManager *mapObjMan);
static MapObjectManager *MapObject_MapObjectManagerDeconst(const MapObject *mapObj);
static const ObjectEvent *ObjectEvent_FindByLocalID(int localID, int objEventCount, const ObjectEvent *objectEvent);
static int ObjectEvent_HasNoScript(const ObjectEvent *objectEvent);
static int ObjectEvent_GetOwnerMapHeaderID(const ObjectEvent *objectEvent);

static const UnkStruct_020EDF0C *MovementType_GetFuncs(u32 movementType);
static UnkFuncPtr_020EDF0C MovementTypeFuncs_GetInitFunc(const UnkStruct_020EDF0C *funcs);
static UnkFuncPtr_020EDF0C_1 MovementTypeFuncs_GetMoveFunc(const UnkStruct_020EDF0C *funcs);
static UnkFuncPtr_020EDF0C_2 MovementTypeFuncs_GetDeleteFunc(const UnkStruct_020EDF0C *funcs);
static MapObjectDrawPauseFunc ObjectEventGfxRenderer_GetPauseFunc(const ObjectEventGfxRenderer *renderer);
static MapObjectDrawResumeFunc ObjectEventGfxRenderer_GetResumeFunc(const ObjectEventGfxRenderer *renderer);

static MapObjectDrawInitFunc ObjectEventGfxRenderer_GetInitFunc(const ObjectEventGfxRenderer *renderer);
static MapObjectDrawFunc ObjectEventGfxRenderer_GetDrawFunc(const ObjectEventGfxRenderer *renderer);
static MapObjectDrawDeleteFunc ObjectEventGfxRenderer_GetDeleteFunc(const ObjectEventGfxRenderer *renderer);
static const ObjectEventGfxRenderer *ObjectEventGfx_FindRenderer(u32 graphicsID);

MapObjectManager *MapObjectMan_New(FieldSystem *fieldSystem, int maxObjs, int taskBasePriority)
{
    MapObjectManager *mapObjMan = MapObjectMan_Alloc(maxObjs);
    MapObjectMan_SetFieldSystem(mapObjMan, fieldSystem);
    MapObjectMan_SetMaxObjects(mapObjMan, maxObjs);
    MapObjectMan_SetTaskBasePriority(mapObjMan, taskBasePriority);

    return mapObjMan;
}

void MapObjectMan_Delete(MapObjectManager *mapObjMan)
{
    Heap_FreeExplicit(HEAP_ID_FIELD2, MapObjectMan_GetMapObject(mapObjMan));
    Heap_FreeExplicit(HEAP_ID_FIELD2, mapObjMan);
}

void MapObjectMan_DeleteObjectsOnMapChange(MapObjectManager *mapObjMan, enum MapHeaderID oldMapHeaderID, enum MapHeaderID newMapHeaderID, int objEventCount, const ObjectEvent *objectEvent)
{
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObject(mapObjMan);

    while (maxObjects) {
        if (MapObject_IsInUse(mapObj) == TRUE) {
            int match = MapObject_FindInObjectEvents(mapObj, newMapHeaderID, objEventCount, objectEvent);

            switch (match) {
            case 0:
                if (MapObject_GetMapHeaderID(mapObj) != newMapHeaderID && !MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_PERSISTENT)) {
                    MapObject_Delete(mapObj);
                }
                break;
            case 2:
                break;
            case 1:
                break;
            }
        }

        mapObj++;
        maxObjects--;
    }

    ov5_021EDA38(mapObjMan, MapObjectMan_GetRenderManager(mapObjMan));
}

static MapObjectManager *MapObjectMan_Alloc(int maxObjs)
{
    int size;
    MapObject *mapObj;
    MapObjectManager *mapObjMan = Heap_Alloc(HEAP_ID_FIELD2, sizeof(MapObjectManager));

    GF_ASSERT(mapObjMan != NULL);
    memset(mapObjMan, 0, sizeof(MapObjectManager));

    size = sizeof(MapObject) * maxObjs;
    mapObj = Heap_Alloc(HEAP_ID_FIELD2, size);

    GF_ASSERT(mapObj != NULL);
    memset(mapObj, 0, size);

    MapObjectMan_SetMapObject(mapObjMan, mapObj);

    return mapObjMan;
}

MapObject *MapObjectMan_AddMapObjectFromHeader(const MapObjectManager *mapObjMan, const ObjectEvent *objectEvent, enum MapHeaderID mapHeaderID)
{
    MapObject *mapObj;
    ObjectEvent objectEventCopy = *objectEvent;
    ObjectEvent *event = &objectEventCopy;

    int localID = ObjectEvent_GetLocalID(event);

    // Reuse an already loaded object if this event refers to one, see the
    // comment above the MapObject struct.
    if (ObjectEvent_HasNoScript(event) == FALSE) {
        mapObj = MapObjectMan_FindBorrowedObject(mapObjMan, localID, mapHeaderID);

        if (mapObj != NULL) {
            MapObject_ReturnToOwnerMap(mapObj, event, mapHeaderID);

            return mapObj;
        }
    } else {
        mapObj = MapObjectMan_FindObjectInMap(mapObjMan, localID, ObjectEvent_GetOwnerMapHeaderID(event));

        if (mapObj != NULL) {
            MapObject_LendToMap(mapObj, mapHeaderID, event);
            return mapObj;
        }
    }

    mapObj = MapObjectMan_GetFreeMapObject(mapObjMan);

    if (mapObj == NULL) {
        return mapObj;
    }

    MapObject_InitFromObjectEvent(mapObj, event, MapObjectMan_FieldSystem(mapObjMan));
    MapObject_InitState(mapObj, mapObjMan);
    MapObject_SetMapHeaderID(mapObj, mapHeaderID);
    MapObject_SetupMovement(mapObj);
    MapObject_InitDraw(mapObj);
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_START_MOVEMENT);
    MapObjectMan_AddMoveTask(mapObjMan, mapObj);
    MapObjectMan_IncObjectCount(MapObjectMan_Deconst(mapObjMan));

    return mapObj;
}

MapObject *MapObjectMan_AddMapObject(const MapObjectManager *mapObjMan, int x, int z, int initialDir, int graphicsID, int movementType, enum MapHeaderID mapHeaderID)
{
    ObjectEvent objectEvent;
    MapObject *mapObj;

    ObjectEvent_SetLocalID(&objectEvent, 0);
    ObjectEvent_SetGraphicsID(&objectEvent, graphicsID);
    ObjectEvent_SetMovementType(&objectEvent, movementType);
    ObjectEvent_SetTrainerType(&objectEvent, 0);
    ObjectEvent_SetHiddenFlag(&objectEvent, 0);
    ObjectEvent_SetScript(&objectEvent, 0);
    ObjectEvent_SetInitialDir(&objectEvent, initialDir);
    ObjectEvent_SetDataAt(&objectEvent, 0, 0);
    ObjectEvent_SetDataAt(&objectEvent, 0, 1);
    ObjectEvent_SetDataAt(&objectEvent, 0, 2);
    ObjectEvent_SetMovementRangeX(&objectEvent, 0);
    ObjectEvent_SetMovementRangeZ(&objectEvent, 0);
    ObjectEvent_SetX(&objectEvent, x);
    ObjectEvent_SetZ(&objectEvent, z);
    ObjectEvent_SetY(&objectEvent, 0);

    mapObj = MapObjectMan_AddMapObjectFromHeader(mapObjMan, &objectEvent, mapHeaderID);

    return mapObj;
}

MapObject *MapObjectMan_AddMapObjectFromLocalID(const MapObjectManager *mapObjMan, int localID, int objEventCount, enum MapHeaderID mapHeaderID, const ObjectEvent *objectEvent)
{
    MapObject *mapObj = NULL;
    const ObjectEvent *event = ObjectEvent_FindByLocalID(localID, objEventCount, objectEvent);

    if (event != NULL) {
        int hiddenFlag = ObjectEvent_GetHiddenFlag(event);
        FieldSystem *fieldSystem = MapObjectMan_FieldSystem(mapObjMan);

        if (!FieldSystem_CheckFlag(fieldSystem, hiddenFlag)) {
            mapObj = MapObjectMan_AddMapObjectFromHeader(mapObjMan, event, mapHeaderID);
        }
    }

    return mapObj;
}

void MapObject_InitGraphics(MapObject *mapObj, int graphicsID)
{
    MapObject_SetGraphicsID(mapObj, graphicsID);
    MapObject_RestartFieldEffects(mapObj);
    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_14);
    MapObject_InitDraw(mapObj);
}

void MapObject_ChangeGraphics(MapObject *mapObj, int graphicsID)
{
    if (MapObject_IsDrawInitialized(mapObj) == TRUE) {
        MapObject_ClearGraphics(mapObj);
    }

    MapObject_InitGraphics(mapObj, graphicsID);
}

void MapObject_Delete(MapObject *mapObj)
{
    const MapObjectManager *mapObjMan = MapObject_MapObjectManager(mapObj);

    if (MapObjectMan_IsDrawInitialized(mapObjMan) == TRUE) {
        MapObject_CallDrawDeleteFunc(mapObj);
    }

    MapObject_CallMoveDeleteFunc(mapObj);
    MapObject_EndMoveTask(mapObj);
    MapObjectMan_DecObjectCount(MapObject_MapObjectManagerDeconst(mapObj));
    MapObject_Clear(mapObj);
}

void MapObject_SetFlagAndDeleteObject(MapObject *mapObj)
{
    int flag = MapObject_GetFlag(mapObj);
    FieldSystem_SetFlag(MapObject_FieldSystem(mapObj), flag);
    MapObject_Delete(mapObj);
}

void MapObject_ClearGraphics(MapObject *mapObj)
{
    const MapObjectManager *mapObjMan = MapObject_MapObjectManager(mapObj);

    if (MapObjectMan_IsDrawInitialized(mapObjMan) == TRUE) {
        if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_14)) {
            MapObject_CallDrawDeleteFunc(mapObj);
        }

        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_14);
    }

    MapObject_SetGraphicsID(mapObj, 0xffff);
    MapObject_SetDrawInitFunc(mapObj, MapObject_DrawInitNoOp);
    MapObject_SetDrawFunc(mapObj, MapObject_DrawNoOp);
    MapObject_SetDrawDeleteFunc(mapObj, MapObject_DrawNoOp);
    MapObject_SetDrawPauseFunc(mapObj, MapObject_DrawPauseNoOp);
    MapObject_SetDrawResumeFunc(mapObj, MapObject_DrawResumeNoOp);
}

void MapObjectMan_DeleteAll(MapObjectManager *mapObjMan)
{
    int i = 0;
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObject(mapObjMan);

    do {
        if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_0)) {
            MapObject_Delete(mapObj);
        }

        mapObj++;
        i++;
    } while (i < maxObjects);
}

void MapObjectMan_PauseAllDrawing(MapObjectManager *mapObjMan)
{
    GF_ASSERT(MapObjectMan_IsDrawInitialized(mapObjMan) == TRUE);

    int i = 0;
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObject(mapObjMan);

    do {
        if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_0) && MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_14)) {
            MapObject_CallDrawPauseFunc(mapObj);
            MapObject_OnDrawPausedNoOp(mapObj);
        }

        mapObj++;
        i++;
    } while (i < maxObjects);
}

void MapObjectMan_ResumeAllDrawing(MapObjectManager *mapObjMan)
{
    GF_ASSERT(MapObjectMan_IsDrawInitialized(mapObjMan) == TRUE);

    int i = 0;
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObject(mapObjMan);

    do {
        if (MapObject_IsInUse(mapObj) == TRUE) {
            if (MapObject_CheckDrawInitializedFlag(mapObj) == TRUE) {
                MapObject_CallDrawResumeFunc(mapObj);
            } else {
                MapObject_InitDraw(mapObj);
            }

            MapObject_RestartFieldEffects(mapObj);
            sub_02064464(mapObj);
        }

        mapObj++;
        i++;
    } while (i < maxObjects);
}

void MapObjectMan_SaveAll(FieldSystem *fieldSystem, const MapObjectManager *mapObjMan, MapObjectSave *mapObjSave, int numSaveSlots)
{
    int index = 0;
    MapObject *mapObj;

    while (MapObjectMan_FindObjectWithStatus(mapObjMan, &mapObj, &index, MAP_OBJ_STATUS_0)) {
        MapObject_Save(fieldSystem, mapObj, mapObjSave);
        mapObjSave++;
        numSaveSlots--;
        GF_ASSERT(numSaveSlots > 0);
    }

    if (numSaveSlots) {
        memset(mapObjSave, 0, numSaveSlots * sizeof(MapObjectSave));
    }
}

void MapObjectMan_LoadAllObjects(const MapObjectManager *mapObjMan, MapObjectSave *mapObjSave, int size)
{
    int unused = 0;
    MapObject *mapObj;

    while (size) {
        if (mapObjSave->status & MAP_OBJ_STATUS_0) {
            mapObj = MapObjectMan_GetFreeMapObject(mapObjMan);
            GF_ASSERT(mapObj != NULL);

            MapObject_LoadSave(mapObj, mapObjSave);
            MapObject_InitAfterLoad(mapObjMan, mapObj);
        }

        mapObjSave++;
        size--;
    }
}

static void MapObject_Save(FieldSystem *fieldSystem, MapObject *mapObj, MapObjectSave *mapObjSave)
{
    mapObjSave->status = MapObject_GetStatus(mapObj);
    mapObjSave->extraStatus = MapObject_GetExtraStatus(mapObj);
    mapObjSave->localID = MapObject_GetLocalID(mapObj);
    mapObjSave->mapID = MapObject_GetMapHeaderID(mapObj);
    mapObjSave->graphicsID = MapObject_GetGraphicsID(mapObj);
    mapObjSave->movementType = MapObject_GetMovementType(mapObj);
    mapObjSave->trainerType = MapObject_GetTrainerType(mapObj);
    mapObjSave->flag = MapObject_GetFlag(mapObj);
    mapObjSave->script = MapObject_GetScript(mapObj);
    mapObjSave->initialDir = MapObject_GetInitialDir(mapObj);
    mapObjSave->facingDir = MapObject_GetFacingDir(mapObj);
    mapObjSave->movingDir = MapObject_GetMovingDir(mapObj);
    mapObjSave->data0 = MapObject_GetDataAt(mapObj, 0);
    mapObjSave->data1 = MapObject_GetDataAt(mapObj, 1);
    mapObjSave->data2 = MapObject_GetDataAt(mapObj, 2);
    mapObjSave->movementRangeX = MapObject_GetMovementRangeX(mapObj);
    mapObjSave->movementRangeZ = MapObject_GetMovementRangeZ(mapObj);
    mapObjSave->xInitial = MapObject_GetXInitial(mapObj);
    mapObjSave->yInitial = MapObject_GetYInitial(mapObj);
    mapObjSave->zInitial = MapObject_GetZInitial(mapObj);
    mapObjSave->x = MapObject_GetX(mapObj);
    mapObjSave->y = MapObject_GetY(mapObj);
    mapObjSave->z = MapObject_GetZ(mapObj);

    VecFx32 pos;
    int heightFound, dynamicHeightCalculationEnabled;

    // Prefer the terrain height at the center of the object's tile over its
    // current height, so it is loaded back standing on the ground.
    VecFx32_SetPosFromMapCoords(mapObjSave->x, mapObjSave->z, &pos);
    pos.y = MapObject_GetPosY(mapObj);

    dynamicHeightCalculationEnabled = MapObject_IsDynamicHeightCalculationEnabled(mapObj);
    heightFound = MapObject_RecalculatePositionHeightEx(fieldSystem, &pos, dynamicHeightCalculationEnabled);

    if (heightFound == FALSE) {
        mapObjSave->posY = MapObject_GetPosY(mapObj);
    } else {
        if (MapObject_IsHeightCalculationDisabled(mapObj) == TRUE) {
            pos.y = MapObject_GetPosY(mapObj);
        }

        mapObjSave->posY = pos.y;
    }

    memcpy(mapObjSave->movementTypeData, MapObject_GetMovementTypeData(mapObj), 16);
    memcpy(mapObjSave->trainerTypeData, MapObject_GetTrainerTypeData(mapObj), 16);
}

static void MapObject_LoadSave(MapObject *mapObj, MapObjectSave *mapObjSave)
{
    MapObject_SetStatus(mapObj, mapObjSave->status);
    MapObject_SetExtraStatus(mapObj, mapObjSave->extraStatus);
    MapObject_SetLocalID(mapObj, mapObjSave->localID);
    MapObject_SetMapHeaderID(mapObj, mapObjSave->mapID);
    MapObject_SetGraphicsID(mapObj, mapObjSave->graphicsID);
    MapObject_SetMovementType(mapObj, mapObjSave->movementType);
    MapObject_SetTrainerType(mapObj, mapObjSave->trainerType);
    MapObject_SetFlag(mapObj, mapObjSave->flag);
    MapObject_SetScript(mapObj, mapObjSave->script);
    MapObject_SetInitialDir(mapObj, mapObjSave->initialDir);
    MapObject_Face(mapObj, mapObjSave->facingDir);
    MapObject_Turn(mapObj, mapObjSave->movingDir);
    MapObject_SetDataAt(mapObj, mapObjSave->data0, 0);
    MapObject_SetDataAt(mapObj, mapObjSave->data1, 1);
    MapObject_SetDataAt(mapObj, mapObjSave->data2, 2);
    MapObject_SetMovementRangeX(mapObj, mapObjSave->movementRangeX);
    MapObject_SetMovementRangeZ(mapObj, mapObjSave->movementRangeZ);
    MapObject_SetXInitial(mapObj, mapObjSave->xInitial);
    MapObject_SetYInitial(mapObj, mapObjSave->yInitial);
    MapObject_SetZInitial(mapObj, mapObjSave->zInitial);
    MapObject_SetX(mapObj, mapObjSave->x);
    MapObject_SetY(mapObj, mapObjSave->y);
    MapObject_SetZ(mapObj, mapObjSave->z);

    // X and Z are derived from the tile coordinates in MapObject_InitPosAfterLoad
    VecFx32 pos = { 0, 0, 0 };

    pos.y = mapObjSave->posY;
    MapObject_SetPos(mapObj, &pos);

    memcpy(MapObject_GetMovementTypeData(mapObj), mapObjSave->movementTypeData, 16);
    memcpy(MapObject_GetTrainerTypeData(mapObj), mapObjSave->trainerTypeData, 16);
}

static void MapObject_InitAfterLoad(const MapObjectManager *mapObjMan, MapObject *mapObj)
{
    MapObject_ResetStatusAfterLoad(mapObj);
    MapObject_InitPosAfterLoad(mapObj);
    MapObject_SetMapObjectManager(mapObj, mapObjMan);
    MapObject_SetMoveFuncs(mapObj);
    sub_020656DC(mapObj);
    MapObject_InitDraw(mapObj);
    MapObjectMan_AddMoveTask(mapObjMan, mapObj);
    MapObject_CallMoveRestoreFunc(mapObj);
    MapObjectMan_IncObjectCount(MapObjectMan_Deconst(mapObjMan));
}

static void MapObject_ResetStatusAfterLoad(MapObject *mapObj)
{
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_0 | MAP_OBJ_STATUS_START_MOVEMENT);
    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_PAUSE_MOVEMENT | MAP_OBJ_STATUS_HIDE | MAP_OBJ_STATUS_14 | MAP_OBJ_STATUS_START_JUMP | MAP_OBJ_STATUS_END_JUMP | MAP_OBJ_STATUS_END_MOVEMENT | MAP_OBJ_STATUS_18 | MAP_OBJ_STATUS_19 | MAP_OBJ_STATUS_21 | MAP_OBJ_STATUS_22 | MAP_OBJ_HEIGHT_CALCULATION_DISABLED);
    MapObject_ClearFieldEffectFlags(mapObj);
}

static void MapObject_InitPosAfterLoad(MapObject *mapObj)
{
    int coord;
    VecFx32 pos;

    MapObject_GetPosPtr(mapObj, &pos);

    coord = MapObject_GetX(mapObj);
    pos.x = MAP_OBJECT_COORD_CENTER_TO_FX32(coord);

    MapObject_SetXPrev(mapObj, coord);
    coord = MapObject_GetY(mapObj);
    MapObject_SetYPrev(mapObj, coord);

    coord = MapObject_GetZ(mapObj);
    pos.z = MAP_OBJECT_COORD_CENTER_TO_FX32(coord);

    MapObject_SetZPrev(mapObj, coord);
    MapObject_SetPos(mapObj, &pos);
}

void MapObjectMan_AddMapObjectsFromHeader(const MapObjectManager *mapObjMan, enum MapHeaderID mapHeaderID, u32 numObjectEvents, const ObjectEvent *objectEvent)
{
    GF_ASSERT(numObjectEvents);

    int size = sizeof(ObjectEvent) * numObjectEvents;
    ObjectEvent *objectEventsCopy = Heap_AllocAtEnd(HEAP_ID_FIELD2, size);

    GF_ASSERT(objectEventsCopy != NULL);
    memcpy(objectEventsCopy, objectEvent, size);

    ObjectEventLoader *loader = Heap_AllocAtEnd(HEAP_ID_FIELD2, sizeof(ObjectEventLoader));
    GF_ASSERT(loader != NULL);

    loader->mapHeaderID = mapHeaderID;
    loader->numObjectEvents = numObjectEvents;
    loader->index = 0;
    loader->mapObjMan = mapObjMan;
    loader->objectEvents = objectEventsCopy;

    ObjectEventLoader_AddMapObjects(loader);
}

static void ObjectEventLoader_AddMapObjects(ObjectEventLoader *loader)
{
    MapObject *mapObj;
    FieldSystem *fieldSystem;
    const ObjectEvent *objectEvent;

    fieldSystem = MapObjectMan_FieldSystem(loader->mapObjMan);
    objectEvent = loader->objectEvents;

    do {
        if (ObjectEvent_HasNoScript(objectEvent) == TRUE || FieldSystem_CheckFlag(fieldSystem, objectEvent->hiddenFlag) == FALSE) {
            mapObj = MapObjectMan_AddMapObjectFromHeader(loader->mapObjMan, objectEvent, loader->mapHeaderID);
            GF_ASSERT(mapObj != NULL);
        }

        objectEvent++;
        loader->index++;
    } while (loader->index < loader->numObjectEvents);

    Heap_FreeExplicit(HEAP_ID_FIELD2, loader->objectEvents);
    Heap_FreeExplicit(HEAP_ID_FIELD2, loader);
}

static MapObject *MapObjectMan_GetFreeMapObject(const MapObjectManager *mapObjMan)
{
    int i = 0;
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObject(mapObjMan);

    do {
        if (!MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_0)) {
            return mapObj;
        }

        mapObj++;
        i++;
    } while (i < maxObjects);

    return NULL;
}

static MapObject *MapObjectMan_FindBorrowedObject(const MapObjectManager *mapObjMan, int localID, int ownerMapHeaderID)
{
    int index = 0;
    MapObject *mapObj;

    while (MapObjectMan_FindObjectWithStatus(mapObjMan, &mapObj, &index, MAP_OBJ_STATUS_0) == TRUE) {
        if (MapObject_IsBorrowed(mapObj) == TRUE
            && MapObject_GetLocalID(mapObj) == localID
            && MapObject_GetOwnerMapHeaderID(mapObj) == ownerMapHeaderID) {
            return mapObj;
        }
    }

    return NULL;
}

static void MapObjectMan_AddMoveTask(const MapObjectManager *mapObjMan, MapObject *mapObj)
{
    int priority = MapObjectMan_GetTaskBasePriority(mapObjMan);
    int movementType = MapObject_GetMovementType(mapObj);
    SysTask *task;

    // Run followers after regular objects so they see where their target moved this frame
    if (movementType == MOVEMENT_TYPE_FOLLOW_PLAYER
        || movementType == MOVEMENT_TYPE_FOLLOW_PARTNER_TRAINER) {
        priority += 2;
    }

    task = SysTask_Start(MapObjectTask_Move, mapObj, priority);
    GF_ASSERT(task != NULL);

    MapObject_SetMoveTask(mapObj, task);
}

static void MapObject_InitFromObjectEvent(MapObject *mapObj, const ObjectEvent *objectEvent, FieldSystem *fieldSystem)
{
    MapObject_SetLocalID(mapObj, ObjectEvent_GetLocalID(objectEvent));
    MapObject_SetGraphicsID(mapObj, MapObject_GetFieldSystemGraphicsID(fieldSystem, ObjectEvent_GetGraphicsID(objectEvent)));
    MapObject_SetMovementType(mapObj, ObjectEvent_GetMovementType(objectEvent));
    MapObject_SetTrainerType(mapObj, ObjectEvent_GetTrainerType(objectEvent));
    MapObject_SetFlag(mapObj, ObjectEvent_GetHiddenFlag(objectEvent));
    MapObject_SetScript(mapObj, ObjectEvent_GetScript(objectEvent));
    MapObject_SetInitialDir(mapObj, ObjectEvent_GetInitialDir(objectEvent));
    MapObject_SetDataAt(mapObj, ObjectEvent_GetDataAt(objectEvent, 0), 0);
    MapObject_SetDataAt(mapObj, ObjectEvent_GetDataAt(objectEvent, 1), 1);
    MapObject_SetDataAt(mapObj, ObjectEvent_GetDataAt(objectEvent, 2), 2);
    MapObject_SetMovementRangeX(mapObj, ObjectEvent_GetMovementRangeX(objectEvent));
    MapObject_SetMovementRangeZ(mapObj, ObjectEvent_GetMovementRangeZ(objectEvent));
    MapObject_InitPosFromObjectEvent(mapObj, objectEvent);
}

static void MapObject_InitPosFromObjectEvent(MapObject *mapObj, const ObjectEvent *objectEvent)
{
    int coord = ObjectEvent_GetX(objectEvent);
    VecFx32 pos;

    pos.x = MAP_OBJECT_COORD_CENTER_TO_FX32(coord);

    MapObject_SetXInitial(mapObj, coord);
    MapObject_SetXPrev(mapObj, coord);
    MapObject_SetX(mapObj, coord);

    // Unlike X and Z, the object event stores Y as an fx32 position
    coord = ObjectEvent_GetY(objectEvent);
    pos.y = (fx32)coord;
    coord = ((coord) >> 3) / FX32_ONE;

    MapObject_SetYInitial(mapObj, coord);
    MapObject_SetYPrev(mapObj, coord);
    MapObject_SetY(mapObj, coord);

    coord = ObjectEvent_GetZ(objectEvent);
    pos.z = MAP_OBJECT_COORD_CENTER_TO_FX32(coord);

    MapObject_SetZInitial(mapObj, coord);
    MapObject_SetZPrev(mapObj, coord);
    MapObject_SetZ(mapObj, coord);
    MapObject_SetPos(mapObj, &pos);
}

static void MapObject_InitState(MapObject *mapObj, const MapObjectManager *mapObjMan)
{
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_0 | MAP_OBJ_STATUS_12 | MAP_OBJ_STATUS_11);

    // The owner of a referenced object wasn't loaded, so spawn it as borrowed
    if (MapObject_HasNoScript(mapObj) == TRUE) {
        MapObject_SetBorrowed(mapObj, TRUE);
    }

    MapObject_SetMapObjectManager(mapObj, mapObjMan);
    MapObject_Face(mapObj, MapObject_GetInitialDir(mapObj));
    MapObject_Turn(mapObj, MapObject_GetInitialDir(mapObj));
    sub_020656DC(mapObj);
}

static void MapObject_SetMoveFuncs(MapObject *mapObj)
{
    const UnkStruct_020EDF0C *funcs = MovementType_GetFuncs(MapObject_GetMovementType(mapObj));

    MapObject_SetMoveInitFunc(mapObj, MovementTypeFuncs_GetInitFunc(funcs));
    MapObject_SetMoveFunc(mapObj, MovementTypeFuncs_GetMoveFunc(funcs));
    MapObject_SetMoveDeleteFunc(mapObj, MovementTypeFuncs_GetDeleteFunc(funcs));
}

static void MapObject_SetDrawFuncs(MapObject *mapObj)
{
    const ObjectEventGfxRenderer *renderer;
    u32 graphicsID = MapObject_GetGraphicsID(mapObj);

    if (graphicsID == OBJ_EVENT_GFX_INVISIBLE) {
        renderer = &gInvisibleObjectEventGfxRenderer;
    } else {
        renderer = ObjectEventGfx_FindRenderer(graphicsID);
    }

    MapObject_SetDrawInitFunc(mapObj, ObjectEventGfxRenderer_GetInitFunc(renderer));
    MapObject_SetDrawFunc(mapObj, ObjectEventGfxRenderer_GetDrawFunc(renderer));
    MapObject_SetDrawDeleteFunc(mapObj, ObjectEventGfxRenderer_GetDeleteFunc(renderer));
    MapObject_SetDrawPauseFunc(mapObj, ObjectEventGfxRenderer_GetPauseFunc(renderer));
    MapObject_SetDrawResumeFunc(mapObj, ObjectEventGfxRenderer_GetResumeFunc(renderer));
}

static void MapObject_Clear(MapObject *mapObj)
{
    memset(mapObj, 0, sizeof(MapObject));
}

// Returns 0 if none of the given object events (belonging to mapHeaderID) refer
// to this object, 1 if the object is already borrowed through one of them and 2
// if it is about to be borrowed by, or returned to, the map.
static int MapObject_FindInObjectEvents(const MapObject *mapObj, int mapHeaderID, int objEventCount, const ObjectEvent *objectEvent)
{
    int localID;
    int ownerMapHeaderID;

    while (objEventCount) {
        localID = ObjectEvent_GetLocalID(objectEvent);

        if (MapObject_GetLocalID(mapObj) == localID) {
            if (ObjectEvent_HasNoScript(objectEvent) == TRUE) {
                ownerMapHeaderID = ObjectEvent_GetOwnerMapHeaderID(objectEvent);

                if (MapObject_IsBorrowed(mapObj) == TRUE) {
                    if (MapObject_GetOwnerMapHeaderID(mapObj) == ownerMapHeaderID) {
                        return 1;
                    }
                } else if (MapObject_GetMapHeaderID(mapObj) == ownerMapHeaderID) {
                    return 2;
                }
            } else if (MapObject_IsBorrowed(mapObj) == TRUE && MapObject_GetOwnerMapHeaderID(mapObj) == mapHeaderID) {
                return 2;
            }
        }

        objEventCount--;
        objectEvent++;
    }

    return 0;
}

static MapObject *MapObjectMan_FindObjectInMap(const MapObjectManager *mapObjMan, int localID, int mapHeaderID)
{
    int index = 0;
    MapObject *mapObj;

    while (MapObjectMan_FindObjectWithStatus(mapObjMan, &mapObj, &index, MAP_OBJ_STATUS_0) == TRUE) {
        if (MapObject_GetLocalID(mapObj) == localID && MapObject_GetMapHeaderID(mapObj) == mapHeaderID) {
            return mapObj;
        }
    }

    return NULL;
}

MapObject *MapObjMan_LocalMapObjByIndex(const MapObjectManager *mapObjMan, int index)
{
    int maxObjects;
    MapObject *mapObj;

    GF_ASSERT(mapObjMan != NULL);

    maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    mapObj = MapObjectMan_GetMapObjectStatic(mapObjMan);

    do {
        if (MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_0) == TRUE && MapObject_IsBorrowed(mapObj) == FALSE
            && MapObject_GetLocalID(mapObj) == index) {
            return mapObj;
        }

        mapObj++;
        maxObjects--;
    } while (maxObjects > 0);

    return NULL;
}

MapObject *MapObjMan_GetLocalMapObjByMovementType(const MapObjectManager *mapObjMan, int movementType)
{
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObjectStatic(mapObjMan);

    do {
        if (MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_0) == TRUE && MapObject_GetMovementType(mapObj) == movementType) {
            return mapObj;
        }

        mapObj++;
        maxObjects--;
    } while (maxObjects > 0);

    return NULL;
}

BOOL MapObjectMan_FindObjectWithStatus(const MapObjectManager *mapObjMan, MapObject **mapObj, int *startIdx, u32 status)
{
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *currMapObj;

    if (*startIdx >= maxObjects) {
        return FALSE;
    }

    currMapObj = MapObjectMan_GetMapObjectStatic(mapObjMan);
    currMapObj = &currMapObj[*startIdx];

    do {
        (*startIdx)++;

        if (MapObject_CheckStatus(currMapObj, status) == status) {
            *mapObj = currMapObj;
            return TRUE;
        }

        currMapObj++;
    } while (*startIdx < maxObjects);

    return FALSE;
}

static void MapObject_RestartFieldEffects(MapObject *mapObj)
{
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_START_MOVEMENT);
    MapObject_ClearFieldEffectFlags(mapObj);
}

static void MapObject_ClearFieldEffectFlags(MapObject *mapObj)
{
    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_SHOW_SHADOW | MAP_OBJ_STATUS_HIDE_SHADOW | MAP_OBJ_STATUS_26 | MAP_OBJ_STATUS_24);
}

static void MapObject_OnDrawPausedNoOp(MapObject *mapObj)
{
    (void)0;
}

static int MapObject_GetFieldSystemGraphicsID(FieldSystem *fieldSystem, int graphicsID)
{
    if (graphicsID >= OBJ_EVENT_GFX_VAR_0 && graphicsID <= OBJ_EVENT_GFX_VAR_F) {
        graphicsID -= OBJ_EVENT_GFX_VAR_0;
        graphicsID = FieldSystem_GetGraphicsID(fieldSystem, graphicsID);
    }

    return graphicsID;
}

static void MapObject_UpdateHeightIfPending(MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_12)) {
        MapObject_RecalculateObjectHeight(mapObj);
    }
}

static void MapObject_SetupMovement(MapObject *mapObj)
{
    MapObject_SetMoveFuncs(mapObj);
    MapObject_InitMove(mapObj);
}

static void MapObject_InitDraw(MapObject *mapObj)
{
    const MapObjectManager *mapObjMan = MapObject_MapObjectManager(mapObj);

    if (MapObjectMan_IsDrawInitialized(mapObjMan) == FALSE) {
        return;
    }

    MapObject_UpdateHeightIfPending(mapObj);
    MapObject_SetSpriteAnimCode(mapObj, 0);
    ov5_021EDD78(mapObj, 0);

    if (MapObject_CheckDrawInitializedFlag(mapObj) == FALSE) {
        MapObject_SetDrawFuncs(mapObj);
        MapObject_CallDrawInitFunc(mapObj);
        MapObject_SetDrawInitialized(mapObj);
    }
}

int MapObject_HasNoScript(const MapObject *mapObj)
{
    u16 script = (u16)MapObject_GetScript(mapObj);

    if (script == 0xffff) {
        return TRUE;
    }

    return FALSE;
}

static void MapObject_ReturnToOwnerMap(MapObject *mapObj, const ObjectEvent *objectEvent, enum MapHeaderID mapHeaderID)
{
    GF_ASSERT(MapObject_IsBorrowed(mapObj) == TRUE);

    MapObject_SetBorrowed(mapObj, FALSE);
    MapObject_SetMapHeaderID(mapObj, mapHeaderID);
    MapObject_SetScript(mapObj, ObjectEvent_GetScript(objectEvent));
    MapObject_SetFlag(mapObj, ObjectEvent_GetHiddenFlag(objectEvent));
}

static void MapObject_LendToMap(MapObject *mapObj, enum MapHeaderID mapHeaderID, const ObjectEvent *objectEvent)
{
    GF_ASSERT(ObjectEvent_HasNoScript(objectEvent) == TRUE);

    MapObject_SetBorrowed(mapObj, TRUE);
    MapObject_SetScript(mapObj, ObjectEvent_GetScript(objectEvent));
    MapObject_SetFlag(mapObj, ObjectEvent_GetOwnerMapHeaderID(objectEvent));
    MapObject_SetMapHeaderID(mapObj, mapHeaderID);
}

int MapObject_CalculateTaskPriority(const MapObject *mapObj, int priority)
{
    int result = MapObject_GetTaskBasePriority(mapObj);
    result += priority;

    return result;
}

// Used by effects and tasks holding on to a map object to detect that its slot
// has since been freed or reused by a different object.
int MapObject_MatchesLocalIDAndMap(const MapObject *mapObj, int localID, enum MapHeaderID mapHeaderID)
{
    if (!MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_0)) {
        return FALSE;
    }

    if (MapObject_GetLocalID(mapObj) != localID) {
        return FALSE;
    }

    if (MapObject_GetMapHeaderID(mapObj) != mapHeaderID) {
        if (MapObject_IsBorrowed(mapObj) == FALSE) {
            return FALSE;
        }

        if (MapObject_GetOwnerMapHeaderID(mapObj) != mapHeaderID) {
            return FALSE;
        }
    }

    return TRUE;
}

int MapObject_MatchesGfxLocalIDAndMap(const MapObject *mapObj, int graphicsID, int localID, enum MapHeaderID mapHeaderID)
{
    if (!MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_0)) {
        return FALSE;
    }

    int effectiveGraphicsID = MapObject_GetEffectiveGraphicsID(mapObj);

    if (effectiveGraphicsID != graphicsID) {
        return FALSE;
    }

    return MapObject_MatchesLocalIDAndMap(mapObj, localID, mapHeaderID);
}

static void MapObjectTask_Move(SysTask *task, void *_mapObject)
{
    MapObject *mapObj = (MapObject *)_mapObject;

    MapObject_Move(mapObj);

    if (MapObject_IsInUse(mapObj) == FALSE) {
        return;
    }

    MapObjectTask_Draw(mapObj);
}

static void MapObjectTask_Draw(MapObject *mapObj)
{
    const MapObjectManager *mapObjMan = MapObject_MapObjectManager(mapObj);

    if (MapObjectMan_IsDrawInitialized(mapObjMan) == TRUE) {
        MapObject_Draw(mapObj);
    }
}

static MapObjectManager *MapObjectMan_Deconst(const MapObjectManager *mapObjMan)
{
    return (MapObjectManager *)mapObjMan;
}

void MapObjectMan_SetMaxObjects(MapObjectManager *mapObjMan, int maxObjs)
{
    mapObjMan->maxObjects = maxObjs;
}

int MapObjectMan_GetMaxObjects(const MapObjectManager *mapObjMan)
{
    return mapObjMan->maxObjects;
}

static void MapObjectMan_IncObjectCount(MapObjectManager *mapObjMan)
{
    mapObjMan->objectCnt++;
}

static void MapObjectMan_DecObjectCount(MapObjectManager *mapObjMan)
{
    mapObjMan->objectCnt--;
}

void MapObjectMan_SetStatusFlagOn(MapObjectManager *mapObjMan, u32 flag)
{
    mapObjMan->status |= flag;
}

void MapObjectMan_SetStatusFlagOff(MapObjectManager *mapObjMan, u32 flag)
{
    mapObjMan->status &= ~flag;
}

u32 MapObjectMan_CheckStatus(const MapObjectManager *mapObjMan, u32 flag)
{
    return mapObjMan->status & flag;
}

void MapObjectMan_SetTaskBasePriority(MapObjectManager *mapObjMan, int basePriority)
{
    mapObjMan->taskBasePriority = basePriority;
}

int MapObjectMan_GetTaskBasePriority(const MapObjectManager *mapObjMan)
{
    return mapObjMan->taskBasePriority;
}

UnkStruct_ov5_021ED0A4 *MapObjectMan_GetRenderManager(const MapObjectManager *mapObjMan)
{
    return &(((MapObjectManager *)mapObjMan)->renderManager);
}

void MapObjectMan_SetMapObject(MapObjectManager *mapObjMan, MapObject *mapObj)
{
    mapObjMan->mapObj = mapObj;
}

const MapObject *MapObjectMan_GetMapObjectConst(const MapObjectManager *mapObjMan)
{
    return mapObjMan->mapObj;
}

static MapObject *MapObjectMan_GetMapObjectStatic(const MapObjectManager *mapObjMan)
{
    return mapObjMan->mapObj;
}

MapObject *MapObjectMan_GetMapObject(const MapObjectManager *mapObjMan)
{
    return mapObjMan->mapObj;
}

void MapObject_Next(const MapObject **mapObj)
{
    (*mapObj)++;
}

void MapObjectMan_SetFieldSystem(MapObjectManager *mapObjMan, FieldSystem *fieldSystem)
{
    mapObjMan->fieldSystem = fieldSystem;
}

FieldSystem *MapObjectMan_FieldSystem(const MapObjectManager *mapObjMan)
{
    return mapObjMan->fieldSystem;
}

void MapObjectMan_SetNARC(MapObjectManager *mapObjMan, NARC *narc)
{
    mapObjMan->narc = narc;
}

NARC *MapObjectMan_GetNARC(const MapObjectManager *mapObjMan)
{
    GF_ASSERT(mapObjMan->narc != NULL);
    return ((MapObjectManager *)mapObjMan)->narc;
}

void MapObject_SetStatus(MapObject *mapObj, u32 status)
{
    mapObj->status = status;
}

u32 MapObject_GetStatus(const MapObject *mapObj)
{
    return mapObj->status;
}

void MapObject_SetStatusFlagOn(MapObject *mapObj, u32 flag)
{
    mapObj->status |= flag;
}

void MapObject_SetStatusFlagOff(MapObject *mapObj, u32 flag)
{
    mapObj->status &= ~flag;
}

u32 MapObject_CheckStatus(const MapObject *mapObj, u32 flag)
{
    return mapObj->status & flag;
}

BOOL MapObject_CheckStatusFlag(const MapObject *mapObj, u32 flag)
{
    return mapObj->status & flag
        ? TRUE
        : FALSE;
}

void MapObject_SetExtraStatus(MapObject *mapObj, u32 extraStatus)
{
    mapObj->extraStatus = extraStatus;
}

u32 MapObject_GetExtraStatus(const MapObject *mapObj)
{
    return mapObj->extraStatus;
}

void MapObject_SetExtraStatusFlagOn(MapObject *mapObj, u32 flag)
{
    mapObj->extraStatus |= flag;
}

void MapObject_SetExtraStatusFlagOff(MapObject *mapObj, u32 flag)
{
    mapObj->extraStatus &= ~flag;
}

u32 MapObject_CheckExtraStatus(const MapObject *mapObj, u32 flag)
{
    return mapObj->extraStatus & flag;
}

void MapObject_SetLocalID(MapObject *mapObj, u32 localID)
{
    mapObj->localID = localID;
}

u32 MapObject_GetLocalID(const MapObject *mapObj)
{
    return mapObj->localID;
}

void MapObject_SetMapHeaderID(MapObject *mapObj, enum MapHeaderID mapHeaderID)
{
    mapObj->mapHeaderID = mapHeaderID;
}

enum MapHeaderID MapObject_GetMapHeaderID(const MapObject *mapObj)
{
    return mapObj->mapHeaderID;
}

void MapObject_SetGraphicsID(MapObject *mapObj, u32 graphicsID)
{
    mapObj->graphicsID = graphicsID;
}

u32 MapObject_GetGraphicsID(const MapObject *mapObj)
{
    return mapObj->graphicsID;
}

u32 MapObject_GetEffectiveGraphicsID(const MapObject *mapObj)
{
    u32 graphicsID = MapObject_GetGraphicsID(mapObj);

    if (BerryPatchGraphics_IsBerryPatch(graphicsID) == TRUE) {
        graphicsID = BerryPatchGraphics_GetCurrentGraphicsResourceID(mapObj);
    }

    return graphicsID;
}

void MapObject_SetMovementType(MapObject *mapObj, u32 movementType)
{
    mapObj->movementType = movementType;
}

u32 MapObject_GetMovementType(const MapObject *mapObj)
{
    return mapObj->movementType;
}

void MapObject_SetTrainerType(MapObject *mapObj, u32 trainerType)
{
    mapObj->trainerType = trainerType;
}

u32 MapObject_GetTrainerType(const MapObject *mapObj)
{
    return mapObj->trainerType;
}

void MapObject_SetFlag(MapObject *mapObj, u32 flag)
{
    mapObj->flag = flag;
}

u32 MapObject_GetFlag(const MapObject *mapObj)
{
    return mapObj->flag;
}

void MapObject_SetScript(MapObject *mapObj, u32 script)
{
    mapObj->script = script;
}

u32 MapObject_GetScript(const MapObject *mapObj)
{
    return mapObj->script;
}

void MapObject_SetInitialDir(MapObject *mapObj, int initialDir)
{
    mapObj->initialDir = initialDir;
}

u32 MapObject_GetInitialDir(const MapObject *mapObj)
{
    return mapObj->initialDir;
}

void MapObject_Face(MapObject *mapObj, int dir)
{
    mapObj->prevFacingDir = mapObj->facingDir;
    mapObj->facingDir = dir;
}

void MapObject_TryFace(MapObject *mapObj, int dir)
{
    if (!MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_LOCK_DIR)) {
        mapObj->prevFacingDir = mapObj->facingDir;
        mapObj->facingDir = dir;
    }
}

int MapObject_GetFacingDir(const MapObject *mapObj)
{
    return mapObj->facingDir;
}

int MapObject_GetPrevFacingDir(const MapObject *mapObj)
{
    return mapObj->prevFacingDir;
}

void MapObject_Turn(MapObject *mapObj, int dir)
{
    mapObj->prevMovingDir = mapObj->movingDir;
    mapObj->movingDir = dir;
}

int MapObject_GetMovingDir(const MapObject *mapObj)
{
    return mapObj->movingDir;
}

void MapObject_TryFaceAndTurn(MapObject *mapObj, int dir)
{
    MapObject_TryFace(mapObj, dir);
    MapObject_Turn(mapObj, dir);
}

void MapObject_SetDataAt(MapObject *mapObj, int value, int index)
{
    switch (index) {
    case 0:
        mapObj->data[0] = value;
        break;
    case 1:
        mapObj->data[1] = value;
        break;
    case 2:
        mapObj->data[2] = value;
        break;
    default:
        GF_ASSERT(FALSE);
    }
}

int MapObject_GetDataAt(const MapObject *mapObj, int index)
{
    switch (index) {
    case 0:
        return mapObj->data[0];
    case 1:
        return mapObj->data[1];
    case 2:
        return mapObj->data[2];
    }

    GF_ASSERT(FALSE);
    return FALSE;
}

void MapObject_SetMovementRangeX(MapObject *mapObj, int movementRangeX)
{
    mapObj->movementRangeX = movementRangeX;
}

int MapObject_GetMovementRangeX(const MapObject *mapObj)
{
    return mapObj->movementRangeX;
}

void MapObject_SetMovementRangeZ(MapObject *mapObj, int movementRangeZ)
{
    mapObj->movementRangeZ = movementRangeZ;
}

int MapObject_GetMovementRangeZ(const MapObject *mapObj)
{
    return mapObj->movementRangeZ;
}

void MapObject_SetSpriteAnimCode(MapObject *mapObj, u32 animCode)
{
    mapObj->spriteAnimCode = animCode;
}

u32 MapObject_GetSpriteAnimCode(const MapObject *mapObj)
{
    return mapObj->spriteAnimCode;
}

void MapObject_SetMoveTask(MapObject *mapObj, SysTask *task)
{
    mapObj->task = task;
}

SysTask *MapObject_GetMoveTask(const MapObject *mapObj)
{
    return mapObj->task;
}

void MapObject_EndMoveTask(const MapObject *mapObj)
{
    SysTask_Done(MapObject_GetMoveTask(mapObj));
}

void MapObject_SetMapObjectManager(MapObject *mapObj, const MapObjectManager *mapObjMan)
{
    mapObj->mapObjMan = mapObjMan;
}

const MapObjectManager *MapObject_MapObjectManager(const MapObject *mapObj)
{
    return mapObj->mapObjMan;
}

static MapObjectManager *MapObject_MapObjectManagerDeconst(const MapObject *mapObj)
{
    return MapObjectMan_Deconst(mapObj->mapObjMan);
}

void *MapObject_InitMovementTypeData(MapObject *mapObj, int size)
{
    void *data;

    GF_ASSERT(size <= 16);

    data = MapObject_GetMovementTypeData(mapObj);
    memset(data, 0, size);

    return data;
}

void *MapObject_GetMovementTypeData(MapObject *mapObj)
{
    return mapObj->movementTypeData;
}

void *MapObject_InitTrainerTypeData(MapObject *mapObj, int size)
{
    u8 *data;

    GF_ASSERT(size <= 16);

    data = MapObject_GetTrainerTypeData(mapObj);
    memset(data, 0, size);

    return data;
}

void *MapObject_GetTrainerTypeData(MapObject *mapObj)
{
    return mapObj->trainerTypeData;
}

void *MapObject_InitMovementData(MapObject *mapObj, int size)
{
    GF_ASSERT(size <= 16);

    void *movementData = MapObject_GetMovementData(mapObj);
    memset(movementData, 0, size);

    return movementData;
}

void *MapObject_GetMovementData(MapObject *mapObj)
{
    return mapObj->movementData;
}

void *MapObject_InitDrawData(MapObject *mapObj, int size)
{
    u8 *data;

    GF_ASSERT(size <= 32);

    data = MapObject_GetDrawData(mapObj);
    memset(data, 0, size);

    return data;
}

void *MapObject_GetDrawData(MapObject *mapObj)
{
    return mapObj->drawData;
}

void MapObject_SetMoveInitFunc(MapObject *mapObj, UnkFuncPtr_020EDF0C func)
{
    mapObj->moveInitFunc = func;
}

void MapObject_CallMoveInitFunc(MapObject *mapObj)
{
    mapObj->moveInitFunc(mapObj);
}

void MapObject_SetMoveFunc(MapObject *mapObj, UnkFuncPtr_020EDF0C_1 func)
{
    mapObj->moveFunc = func;
}

void MapObject_CallMoveFunc(MapObject *mapObj)
{
    mapObj->moveFunc(mapObj);
}

void MapObject_SetMoveDeleteFunc(MapObject *mapObj, UnkFuncPtr_020EDF0C_2 func)
{
    mapObj->moveDeleteFunc = func;
}

void MapObject_CallMoveDeleteFunc(MapObject *mapObj)
{
    mapObj->moveDeleteFunc(mapObj);
}

void MapObject_CallMoveRestoreFunc(MapObject *mapObj)
{
    const UnkStruct_020EDF0C *funcs = MovementType_GetFuncs(MapObject_GetMovementType(mapObj));
    funcs->unk_10(mapObj);
}

void MapObject_SetDrawInitFunc(MapObject *mapObj, MapObjectDrawInitFunc func)
{
    mapObj->drawInitFunc = func;
}

void MapObject_CallDrawInitFunc(MapObject *mapObj)
{
    mapObj->drawInitFunc(mapObj);
}

void MapObject_SetDrawFunc(MapObject *mapObj, MapObjectDrawFunc func)
{
    mapObj->drawFunc = func;
}

void MapObject_CallDrawFunc(MapObject *mapObj)
{
    mapObj->drawFunc(mapObj);
}

void MapObject_SetDrawDeleteFunc(MapObject *mapObj, MapObjectDrawDeleteFunc func)
{
    mapObj->drawDeleteFunc = func;
}

void MapObject_CallDrawDeleteFunc(MapObject *mapObj)
{
    mapObj->drawDeleteFunc(mapObj);
}

void MapObject_SetDrawPauseFunc(MapObject *mapObj, MapObjectDrawPauseFunc func)
{
    mapObj->drawPauseFunc = func;
}

void MapObject_CallDrawPauseFunc(MapObject *mapObj)
{
    mapObj->drawPauseFunc(mapObj);
}

void MapObject_SetDrawResumeFunc(MapObject *mapObj, MapObjectDrawResumeFunc func)
{
    mapObj->drawResumeFunc = func;
}

void MapObject_CallDrawResumeFunc(MapObject *mapObj)
{
    mapObj->drawResumeFunc(mapObj);
}

void MapObject_SetMovementAction(MapObject *mapObj, enum MovementAction movementAction)
{
    mapObj->movementAction = movementAction;
}

enum MovementAction MapObject_GetMovementAction(const MapObject *mapObj)
{
    return mapObj->movementAction;
}

void MapObject_SetMovementStep(MapObject *mapObj, int movementStep)
{
    mapObj->movementStep = movementStep;
}

void MapObject_AdvanceMovementStep(MapObject *mapObj)
{
    mapObj->movementStep++;
}

int MapObject_GetMovementStep(const MapObject *mapObj)
{
    return mapObj->movementStep;
}

void MapObject_SetCurrTileBehavior(MapObject *mapObj, u32 tileBehavior)
{
    mapObj->currTileBehavior = tileBehavior;
}

u32 MapObject_GetCurrTileBehavior(const MapObject *mapObj)
{
    return mapObj->currTileBehavior;
}

void MapObject_SetPrevTileBehavior(MapObject *mapObj, u32 tileBehavior)
{
    mapObj->prevTileBehavior = tileBehavior;
}

u32 MapObject_GetPrevTileBehavior(const MapObject *mapObj)
{
    return mapObj->prevTileBehavior;
}

FieldSystem *MapObject_FieldSystem(const MapObject *mapObj)
{
    MapObjectManager *mapObjMan = MapObject_MapObjectManagerDeconst(mapObj);
    return MapObjectMan_FieldSystem(mapObjMan);
}

int MapObject_GetTaskBasePriority(const MapObject *mapObj)
{
    return MapObjectMan_GetTaskBasePriority(MapObject_MapObjectManager(mapObj));
}

int MapObject_GetOwnerMapHeaderID(const MapObject *mapObj)
{
    GF_ASSERT(MapObject_IsBorrowed(mapObj) == TRUE);
    return MapObject_GetFlag(mapObj);
}

// The manager status borrows the map object status constants, but its bits
// mean: 0 = drawing initialized, 1 = movement stopped, 2 = drawing stopped and
// 3 = shadows disabled.
void MapObjectMan_StopAllMovement(MapObjectManager *mapObjMan)
{
    MapObjectMan_SetStatusFlagOn(mapObjMan, MAP_OBJ_STATUS_1 | MAP_OBJ_STATUS_START_MOVEMENT);
}

void MapObjectMan_StartAllMovement(MapObjectManager *mapObjMan)
{
    MapObjectMan_SetStatusFlagOff(mapObjMan, MAP_OBJ_STATUS_1 | MAP_OBJ_STATUS_START_MOVEMENT);
}

void MapObjectMan_PauseAllMovement(MapObjectManager *mapObjMan)
{
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObject(mapObjMan);

    do {
        if (MapObject_IsInUse(mapObj)) {
            MapObject_SetPauseMovementOn(mapObj);
        }

        mapObj++;
        maxObjects--;
    } while (maxObjects);
}

void MapObjectMan_UnpauseAllMovement(MapObjectManager *mapObjMan)
{
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObject(mapObjMan);

    do {
        if (MapObject_IsInUse(mapObj)) {
            MapObject_SetPauseMovementOff(mapObj);
        }

        mapObj++;
        maxObjects--;
    } while (maxObjects);
}

int MapObjectMan_IsDrawInitialized(const MapObjectManager *mapObjMan)
{
    if (MapObjectMan_CheckStatus(mapObjMan, MAP_OBJ_STATUS_0)) {
        return TRUE;
    }

    return FALSE;
}

u32 MapObject_CheckManagerStatus(const MapObject *mapObj, u32 flag)
{
    const MapObjectManager *mapObjMan = MapObject_MapObjectManager(mapObj);

    return MapObjectMan_CheckStatus(mapObjMan, flag);
}

void MapObjectMan_SetShadowsEnabled(MapObjectManager *mapObjMan, int enabled)
{
    if (enabled == FALSE) {
        MapObjectMan_SetStatusFlagOn(mapObjMan, MAP_OBJ_STATUS_END_MOVEMENT);
    } else {
        MapObjectMan_SetStatusFlagOff(mapObjMan, MAP_OBJ_STATUS_END_MOVEMENT);
    }
}

int MapObjectMan_AreShadowsEnabled(const MapObjectManager *mapObjMan)
{
    if (MapObjectMan_CheckStatus(mapObjMan, MAP_OBJ_STATUS_END_MOVEMENT)) {
        return FALSE;
    }

    return TRUE;
}

int MapObject_IsInUse(const MapObject *mapObj)
{
    return MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_0);
}

void MapObject_SetMoving(MapObject *mapObj)
{
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_1);
}

void MapObject_ClearMoving(MapObject *mapObj)
{
    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_1);
}

int MapObject_IsMoving(const MapObject *mapObj)
{
    return MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_1);
}

void MapObject_SetStartMovement(MapObject *mapObj)
{
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_START_MOVEMENT);
}

void MapObject_SetEndMovementOff(MapObject *mapObj)
{
    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_END_MOVEMENT);
}

void MapObject_SetDrawInitialized(MapObject *mapObj)
{
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_14);
}

int MapObject_CheckDrawInitializedFlag(const MapObject *mapObj)
{
    return MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_14);
}

int MapObject_IsHidden(const MapObject *mapObj)
{
    return MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_HIDE);
}

void MapObject_SetHidden(MapObject *mapObj, int hidden)
{
    if (hidden == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_HIDE);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_HIDE);
    }
}

void MapObject_SetCollisionEnabled(MapObject *mapObj, int enabled)
{
    if (enabled == TRUE) {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_18);
    } else {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_18);
    }
}

int MapObject_IsInteractable(MapObject *mapObj)
{
    if (MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_19) == TRUE) {
        return FALSE;
    }

    return TRUE;
}

void MapObject_SetInteractionDisabled(MapObject *mapObj, int disabled)
{
    if (disabled == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_19);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_19);
    }
}

void MapObject_SetPauseMovementOn(MapObject *mapObj)
{
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_PAUSE_MOVEMENT);
}

void MapObject_SetPauseMovementOff(MapObject *mapObj)
{
    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_PAUSE_MOVEMENT);
}

int MapObject_IsMovementPaused(const MapObject *mapObj)
{
    if (MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_PAUSE_MOVEMENT) == TRUE) {
        return TRUE;
    }

    return FALSE;
}

int MapObject_IsDrawInitialized(const MapObject *mapObj)
{
    const MapObjectManager *mapObjMan = MapObject_MapObjectManager(mapObj);

    if (MapObjectMan_IsDrawInitialized(mapObjMan) == FALSE) {
        return FALSE;
    }

    if (!MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_14)) {
        return FALSE;
    }

    return TRUE;
}

void MapObject_SetHeightCalculationDisabled(MapObject *mapObj, BOOL heightCalculationDisabled)
{
    if (heightCalculationDisabled == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_HEIGHT_CALCULATION_DISABLED);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_HEIGHT_CALCULATION_DISABLED);
    }
}

int MapObject_IsHeightCalculationDisabled(const MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_HEIGHT_CALCULATION_DISABLED)) {
        return TRUE;
    }

    return FALSE;
}

void MapObject_SetFlagIsPersistent(MapObject *mapObj, BOOL flag)
{
    if (flag == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_PERSISTENT);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_PERSISTENT);
    }
}

void MapObject_SetBorrowed(MapObject *mapObj, int borrowed)
{
    if (borrowed == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_25);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_25);
    }
}

int MapObject_IsBorrowed(const MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_25)) {
        return TRUE;
    }

    return FALSE;
}

void MapObject_SetShallowWaterEffectActive(MapObject *mapObj, int active)
{
    if (active == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_26);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_26);
    }
}

int MapObject_IsShallowWaterEffectActive(const MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_26)) {
        return TRUE;
    }

    return FALSE;
}

void MapObject_SetFlagDoNotSinkIntoTerrain(MapObject *mapObj, BOOL flag)
{
    if (flag == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_DO_NOT_SINK_INTO_TERRAIN);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_DO_NOT_SINK_INTO_TERRAIN);
    }
}

int MapObject_CheckFlagDoNotSinkIntoTerrain(const MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_DO_NOT_SINK_INTO_TERRAIN)) {
        return TRUE;
    }

    return FALSE;
}

void MapObject_SetElevatedBridgeStatus(MapObject *mapObj, BOOL isOnElevatedBridge)
{
    if (isOnElevatedBridge == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_ON_ELEVATED_BRIDGE);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_ON_ELEVATED_BRIDGE);
    }
}

int MapObject_IsStatusOnElevatedBridge(const MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_ON_ELEVATED_BRIDGE)) {
        return TRUE;
    }

    return FALSE;
}

void MapObject_SetReflectionActive(MapObject *mapObj, int active)
{
    if (active == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_24);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_24);
    }
}

int MapObject_IsReflectionActive(const MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_24)) {
        return TRUE;
    }

    return FALSE;
}

int MapObject_IsMovementActionSet(const MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_4)) {
        return TRUE;
    }

    return FALSE;
}

void MapObject_SetDynamicHeightCalculationEnabled(MapObject *mapObj, int enabled)
{
    if (enabled == TRUE) {
        MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_DYNAMIC_HEIGHT_CALCULATION_ENABLED);
    } else {
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_DYNAMIC_HEIGHT_CALCULATION_ENABLED);
    }
}

int MapObject_IsDynamicHeightCalculationEnabled(const MapObject *mapObj)
{
    if (MapObject_CheckStatus(mapObj, MAP_OBJ_DYNAMIC_HEIGHT_CALCULATION_ENABLED)) {
        return TRUE;
    }

    return FALSE;
}

void MapObject_SetTileBehaviorCheckDisabled(MapObject *mapObj, int disabled)
{
    if (disabled == TRUE) {
        MapObject_SetExtraStatusFlagOn(mapObj, 1 << 2);
    } else {
        MapObject_SetExtraStatusFlagOff(mapObj, 1 << 2);
    }
}

int MapObject_IsTileBehaviorCheckDisabled(const MapObject *mapObj)
{
    if (MapObject_CheckExtraStatus(mapObj, 1 << 2)) {
        return TRUE;
    }

    return FALSE;
}

int MapObject_GetXInitial(const MapObject *mapObj)
{
    return mapObj->xInitial;
}

void MapObject_SetXInitial(MapObject *mapObj, int x)
{
    mapObj->xInitial = x;
}

int MapObject_GetYInitial(const MapObject *mapObj)
{
    return mapObj->yInitial;
}

void MapObject_SetYInitial(MapObject *mapObj, int y)
{
    mapObj->yInitial = y;
}

int MapObject_GetZInitial(const MapObject *mapObj)
{
    return mapObj->zInitial;
}

void MapObject_SetZInitial(MapObject *mapObj, int z)
{
    mapObj->zInitial = z;
}

int MapObject_GetXPrev(const MapObject *mapObj)
{
    return mapObj->xPrev;
}

void MapObject_SetXPrev(MapObject *mapObj, int x)
{
    mapObj->xPrev = x;
}

int MapObject_GetYPrev(const MapObject *mapObj)
{
    return mapObj->yPrev;
}

void MapObject_SetYPrev(MapObject *mapObj, int y)
{
    mapObj->yPrev = y;
}

int MapObject_GetZPrev(const MapObject *mapObj)
{
    return mapObj->zPrev;
}

void MapObject_SetZPrev(MapObject *mapObj, int z)
{
    mapObj->zPrev = z;
}

int MapObject_GetX(const MapObject *mapObj)
{
    return mapObj->x;
}

void MapObject_SetX(MapObject *mapObj, int x)
{
    mapObj->x = x;
}

void MapObject_AddX(MapObject *mapObj, int dx)
{
    mapObj->x += dx;
}

int MapObject_GetY(const MapObject *mapObj)
{
    return mapObj->y;
}

void MapObject_SetY(MapObject *mapObj, int y)
{
    mapObj->y = y;
}

void MapObject_AddY(MapObject *mapObj, int dy)
{
    mapObj->y += dy;
}

int MapObject_GetZ(const MapObject *mapObj)
{
    return mapObj->z;
}

void MapObject_SetZ(MapObject *mapObj, int z)
{
    mapObj->z = z;
}

void MapObject_AddZ(MapObject *mapObj, int dz)
{
    mapObj->z += dz;
}

void MapObject_GetPosPtr(const MapObject *mapObj, VecFx32 *pos)
{
    *pos = mapObj->pos;
}

void MapObject_SetPos(MapObject *mapObj, const VecFx32 *pos)
{
    mapObj->pos = *pos;
}

const VecFx32 *MapObject_GetPos(const MapObject *mapObj)
{
    return &mapObj->pos;
}

fx32 MapObject_GetPosY(const MapObject *mapObj)
{
    return mapObj->pos.y;
}

void MapObject_GetSpriteJumpOffset(const MapObject *mapObj, VecFx32 *vec)
{
    *vec = mapObj->spriteJumpOffset;
}

void MapObject_SetSpriteJumpOffset(MapObject *mapObj, const VecFx32 *vec)
{
    mapObj->spriteJumpOffset = *vec;
}

VecFx32 *MapObject_GetSpriteJumpOffset1(MapObject *mapObj)
{
    return &mapObj->spriteJumpOffset;
}

void MapObject_GetSpritePosOffset(const MapObject *mapObj, VecFx32 *vec)
{
    *vec = mapObj->spritePosOffset;
}

void MapObject_SetSpritePosOffset(MapObject *mapObj, const VecFx32 *vec)
{
    mapObj->spritePosOffset = *vec;
}

void MapObject_GetSpriteTerrainOffset(const MapObject *mapObj, VecFx32 *spriteOffset)
{
    *spriteOffset = mapObj->spriteTerrainOffset;
}

void MapObject_SetSpriteTerrainOffset(MapObject *mapObj, const VecFx32 *spriteOffset)
{
    mapObj->spriteTerrainOffset = *spriteOffset;
}

int MapObject_GetYFromPos(const MapObject *mapObj)
{
    fx32 posY = MapObject_GetPosY(mapObj);
    int y = ((posY) >> 3) / FX32_ONE;

    return y;
}

void ObjectEvent_SetLocalID(ObjectEvent *objectEvent, int localID)
{
    objectEvent->localID = localID;
}

int ObjectEvent_GetLocalID(const ObjectEvent *objectEvent)
{
    return objectEvent->localID;
}

void ObjectEvent_SetGraphicsID(ObjectEvent *objectEvent, int graphicsID)
{
    objectEvent->graphicsID = graphicsID;
}

int ObjectEvent_GetGraphicsID(const ObjectEvent *objectEvent)
{
    return objectEvent->graphicsID;
}

void ObjectEvent_SetMovementType(ObjectEvent *objectEvent, int movementType)
{
    objectEvent->movementType = movementType;
}

int ObjectEvent_GetMovementType(const ObjectEvent *objectEvent)
{
    return objectEvent->movementType;
}

void ObjectEvent_SetTrainerType(ObjectEvent *objectEvent, int trainerType)
{
    objectEvent->trainerType = trainerType;
}

int ObjectEvent_GetTrainerType(const ObjectEvent *objectEvent)
{
    return objectEvent->trainerType;
}

void ObjectEvent_SetHiddenFlag(ObjectEvent *objectEvent, int flag)
{
    objectEvent->hiddenFlag = flag;
}

int ObjectEvent_GetHiddenFlag(const ObjectEvent *objectEvent)
{
    return objectEvent->hiddenFlag;
}

void ObjectEvent_SetScript(ObjectEvent *objectEvent, int script)
{
    objectEvent->script = script;
}

int ObjectEvent_GetScript(const ObjectEvent *objectEvent)
{
    return objectEvent->script;
}

void ObjectEvent_SetInitialDir(ObjectEvent *objectEvent, int initialDir)
{
    objectEvent->dir = initialDir;
}

int ObjectEvent_GetInitialDir(const ObjectEvent *objectEvent)
{
    return objectEvent->dir;
}

void ObjectEvent_SetDataAt(ObjectEvent *objectEvent, int value, int index)
{
    switch (index) {
    case 0:
        objectEvent->data[0] = value;
        break;
    case 1:
        objectEvent->data[1] = value;
        break;
    case 2:
        objectEvent->data[2] = value;
        break;
    default:
        GF_ASSERT(FALSE);
    }
}

int ObjectEvent_GetDataAt(const ObjectEvent *objectEvent, int index)
{
    switch (index) {
    case 0:
        return objectEvent->data[0];
    case 1:
        return objectEvent->data[1];
    case 2:
        return objectEvent->data[2];
    }

    GF_ASSERT(FALSE);
    return FALSE;
}

void ObjectEvent_SetMovementRangeX(ObjectEvent *objectEvent, int movementRangeX)
{
    objectEvent->movementRangeX = movementRangeX;
}

int ObjectEvent_GetMovementRangeX(const ObjectEvent *objectEvent)
{
    return objectEvent->movementRangeX;
}

void ObjectEvent_SetMovementRangeZ(ObjectEvent *objectEvent, int movementRangeZ)
{
    objectEvent->movementRangeZ = movementRangeZ;
}

int ObjectEvent_GetMovementRangeZ(const ObjectEvent *objectEvent)
{
    return objectEvent->movementRangeZ;
}

void ObjectEvent_SetX(ObjectEvent *objectEvent, int x)
{
    objectEvent->x = x;
}

int ObjectEvent_GetX(const ObjectEvent *objectEvent)
{
    return objectEvent->x;
}

void ObjectEvent_SetY(ObjectEvent *objectEvent, int y)
{
    objectEvent->y = y;
}

int ObjectEvent_GetY(const ObjectEvent *objectEvent)
{
    return objectEvent->y;
}

void ObjectEvent_SetZ(ObjectEvent *objectEvent, int z)
{
    objectEvent->z = z;
}

int ObjectEvent_GetZ(const ObjectEvent *objectEvent)
{
    return objectEvent->z;
}

static const ObjectEvent *ObjectEvent_FindByLocalID(int localID, int objEventCount, const ObjectEvent *objectEvent)
{
    int i = 0;

    do {
        if (ObjectEvent_HasNoScript(&objectEvent[i]) == FALSE && ObjectEvent_GetLocalID(&objectEvent[i]) == localID) {
            return &objectEvent[i];
        }

        i++;
    } while (i < objEventCount);

    return NULL;
}

static int ObjectEvent_HasNoScript(const ObjectEvent *objectEvent)
{
    u16 script = (u16)ObjectEvent_GetScript(objectEvent);

    if (script == 0xffff) {
        return TRUE;
    }

    return FALSE;
}

static int ObjectEvent_GetOwnerMapHeaderID(const ObjectEvent *objectEvent)
{
    GF_ASSERT(ObjectEvent_HasNoScript(objectEvent) == TRUE);
    return ObjectEvent_GetHiddenFlag(objectEvent);
}

static const UnkStruct_020EDF0C *MovementType_GetFuncs(u32 movementType)
{
    GF_ASSERT(movementType < MAX_MOVEMENT_TYPE);
    return Unk_020EE3A8[movementType];
}

static UnkFuncPtr_020EDF0C MovementTypeFuncs_GetInitFunc(const UnkStruct_020EDF0C *funcs)
{
    return funcs->unk_04;
}

static UnkFuncPtr_020EDF0C_1 MovementTypeFuncs_GetMoveFunc(const UnkStruct_020EDF0C *funcs)
{
    return funcs->unk_08;
}

static UnkFuncPtr_020EDF0C_2 MovementTypeFuncs_GetDeleteFunc(const UnkStruct_020EDF0C *funcs)
{
    return funcs->unk_0C;
}

static MapObjectDrawInitFunc ObjectEventGfxRenderer_GetInitFunc(const ObjectEventGfxRenderer *renderer)
{
    return renderer->initFunc;
}

static MapObjectDrawFunc ObjectEventGfxRenderer_GetDrawFunc(const ObjectEventGfxRenderer *renderer)
{
    return renderer->drawFunc;
}

static MapObjectDrawDeleteFunc ObjectEventGfxRenderer_GetDeleteFunc(const ObjectEventGfxRenderer *renderer)
{
    return renderer->deleteFunc;
}

static MapObjectDrawPauseFunc ObjectEventGfxRenderer_GetPauseFunc(const ObjectEventGfxRenderer *renderer)
{
    return renderer->pauseFunc;
}

static MapObjectDrawResumeFunc ObjectEventGfxRenderer_GetResumeFunc(const ObjectEventGfxRenderer *renderer)
{
    return renderer->resumeFunc;
}

static const ObjectEventGfxRenderer *ObjectEventGfx_FindRenderer(u32 graphicsID)
{
    const ObjectEventGfxRendererEntry *entry = gObjectEventGfxRenderersTable;

    do {
        if (entry->graphicsID == graphicsID) {
            return entry->renderer;
        }

        entry++;
    } while (entry->graphicsID != OBJ_EVENT_GFX_SENTINEL_ID);

    GF_ASSERT(FALSE);
    return NULL;
}

MapObject *MapObjectMan_FindObjectAtCoords(const MapObjectManager *mapObjMan, int x, int z, int checkPrevPos)
{
    int maxObjects = MapObjectMan_GetMaxObjects(mapObjMan);
    MapObject *mapObj = MapObjectMan_GetMapObject(mapObjMan);

    do {
        if (MapObject_CheckStatus(mapObj, MAP_OBJ_STATUS_0)) {
            if (checkPrevPos && MapObject_GetXPrev(mapObj) == x && MapObject_GetZPrev(mapObj) == z) {
                return mapObj;
            }

            if (MapObject_GetX(mapObj) == x && MapObject_GetZ(mapObj) == z) {
                return mapObj;
            }
        }

        mapObj++;
        maxObjects--;
    } while (maxObjects);

    return NULL;
}

void MapObject_SetPosDirFromVec(MapObject *mapObj, const VecFx32 *pos, int dir)
{
    int x, y, z;

    x = ((pos->x) >> 4) / FX32_ONE;
    MapObject_SetX(mapObj, x);

    y = ((pos->y) >> 3) / FX32_ONE;
    MapObject_SetY(mapObj, y);

    z = ((pos->z) >> 4) / FX32_ONE;
    MapObject_SetZ(mapObj, z);

    MapObject_SetPos(mapObj, pos);
    MapObject_UpdateCoords(mapObj);

    MapObject_Face(mapObj, dir);

    sub_020656DC(mapObj);
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_START_MOVEMENT);
    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_1 | MAP_OBJ_STATUS_END_MOVEMENT);
}

void MapObject_SetPosDirFromCoords(MapObject *mapObj, int x, int y, int z, int dir)
{
    VecFx32 pos;

    pos.x = ((x << 4) * FX32_ONE) + ((16 * FX32_ONE) >> 1);
    MapObject_SetX(mapObj, x);

    pos.y = ((y << 3) * FX32_ONE) + 0;
    MapObject_SetY(mapObj, y);

    pos.z = ((z << 4) * FX32_ONE) + ((16 * FX32_ONE) >> 1);
    MapObject_SetZ(mapObj, z);

    MapObject_SetPos(mapObj, &pos);
    MapObject_UpdateCoords(mapObj);

    MapObject_Face(mapObj, dir);

    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_START_MOVEMENT);
    MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_1 | MAP_OBJ_STATUS_END_MOVEMENT);

    sub_020656DC(mapObj);
}

void MapObject_SwitchMovementType(MapObject *mapObj, u32 movementType)
{
    MapObject_CallMoveDeleteFunc(mapObj);
    MapObject_SetMovementType(mapObj, movementType);
    MapObject_SetMoveFuncs(mapObj);
    MapObject_InitMove(mapObj);
}

void MapObject_ChangeLocalID(MapObject *mapObj, int localID)
{
    MapObject_SetLocalID(mapObj, localID);

    MapObject_SetStartMovement(mapObj);
    MapObject_ClearFieldEffectFlags(mapObj);
}

void MapObject_MoveInitNoOp(MapObject *mapObj)
{
    return;
}

void MapObject_MoveNoOp(MapObject *mapObj)
{
    return;
}

void MapObject_MoveDeleteNoOp(MapObject *mapObj)
{
    return;
}

void MapObject_MoveRestoreNoOp(MapObject *mapObj)
{
    return;
}

void MapObject_DrawInitNoOp(MapObject *mapObj)
{
    return;
}

void MapObject_DrawNoOp(MapObject *mapObj)
{
    return;
}

void MapObject_DrawPauseNoOp(MapObject *mapObj)
{
    return;
}

void MapObject_DrawResumeNoOp(MapObject *mapObj)
{
    return;
}
