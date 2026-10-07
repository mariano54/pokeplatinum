#ifndef POKEPLATINUM_ENTRANCE_ANIMATION_H
#define POKEPLATINUM_ENTRANCE_ANIMATION_H

#include "field/field_system_decl.h"
#include "overlay005/entrance_animation_decl.h"

#include "field_task.h"

#define DOOR_SOUND_EFFECT_TYPE_HINGED                    0
#define DOOR_SOUND_EFFECT_TYPE_SLIDING                   1
#define DOOR_SOUND_EFFECT_TYPE_VEILSTONE_DPT_STORE_CHIME 2

EntranceAnimation *EntranceAnimation_New(void);
void EntranceAnimation_Free(EntranceAnimation *anim);
void EntranceAnimation_SetPosition(const int x, const int z, EntranceAnimation *anim);
BOOL EntranceAnimation_EnterDoor(FieldSystem *fieldSystem, EntranceAnimation *anim);
BOOL EntranceAnimation_ExitDoor(FieldSystem *fieldSystem, EntranceAnimation *anim);
BOOL EntranceAnimation_ArriveByEscalator(FieldSystem *fieldSystem, EntranceAnimation *anim, const u8 playerDir);
BOOL EntranceAnimation_DepartByEscalator(FieldSystem *fieldSystem, EntranceAnimation *anim, const u8 playerDir);
void DoorAnimation_FindDoorAndLoad(FieldSystem *fieldSystem, const int x, const int z, const u8 tag);
void DoorAnimation_PlayOpenAnimation(FieldSystem *fieldSystem, const u8 tag);
void DoorAnimation_PlayCloseAnimation(FieldSystem *fieldSystem, const u8 tag);
void FieldSystem_WaitForAnimation(FieldSystem *fieldSystem, const u8 tag);
void FieldSystem_UnloadAnimation(FieldSystem *fieldSystem, const u8 tag);
void BikeSlope_PlayPropAnimation(const int x, const int z, const int animIndex, FieldSystem *fieldSystem);
EntranceFade *EntranceFade_New(void);
BOOL EntranceFade_ArriveFromWhite(FieldTask *task);
BOOL EntranceFade_DepartToWhite(FieldTask *task);
BOOL EntranceFade_DepartWithCircle(FieldTask *task);
BOOL EntranceFade_ArriveWithWipe(FieldTask *task);
BOOL EntranceFade_Arrive(FieldTask *task);

#endif // POKEPLATINUM_ENTRANCE_ANIMATION_H
