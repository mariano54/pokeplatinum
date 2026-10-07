#ifndef POKEPLATINUM_PLAYER_AVATAR_ACTIONS_H
#define POKEPLATINUM_PLAYER_AVATAR_ACTIONS_H

#include "field/field_system_decl.h"

#include "field_task.h"
#include "location.h"
#include "player_avatar.h"
#include "sys_task_manager.h"

void PlayerAvatar_SetTransitionState(PlayerAvatar *playerAvatar, u32 param1);
void PlayerAvatar_RequestChangeState(PlayerAvatar *playerAvatar);
int PlayerAvatar_TryStartMoveEvent(FieldSystem *fieldSystem, PlayerAvatar *playerAvatar, enum FaceDirection dir, int param3);
void FieldTask_StartUseSurf(FieldTask *task, int direction, int partySlot);
BOOL PlayerAvatar_CanUseSurf(PlayerAvatar *playerAvatar, u32 currTileBehavior, u32 nextTileBehavior);
void FieldTask_StartUseRockClimb(FieldTask *task, int direction, int partySlot);
BOOL PlayerAvatar_CanUseRockClimb(u32 metatileBehavior, int facingDir);
void FieldSystem_StartUseWaterfallNoCutIn(FieldSystem *fieldSystem, int param1);
void FieldTask_StartUseWaterfall(FieldTask *task, int direction, int partySlot);
void FieldTask_StartChangeIntoContestAttire(FieldTask *param0);
void PlayerAvatar_TryStartLookingAtPoketch(PlayerAvatar *playerAvatar);
void PlayerAvatar_TryStopLookingAtPoketch(PlayerAvatar *playerAvatar);
SysTask *FieldSystem_StartSavePoseTask(FieldSystem *fieldSystem);
void FieldSystem_EndSavePoseTask(SysTask *param0);
SysTask *FieldSystem_StartVsSeekerTask(FieldSystem *fieldSystem);
void FieldSystem_EndVsSeekerTask(SysTask *param0);

#endif // POKEPLATINUM_PLAYER_AVATAR_ACTIONS_H
