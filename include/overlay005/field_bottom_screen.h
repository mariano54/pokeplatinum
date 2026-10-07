#ifndef POKEPLATINUM_FIELD_BOTTOM_SCREEN_H
#define POKEPLATINUM_FIELD_BOTTOM_SCREEN_H

#include "field/field_system_decl.h"

void FieldSystem_InitBottomScreen(FieldSystem *fieldSystem);
BOOL FieldSystem_IsBottomScreenRunningDummy(FieldSystem *fieldSystem);
void FieldSystem_EndBottomScreen(FieldSystem *fieldSystem);
BOOL FieldSystem_IsBottomScreenDone(FieldSystem *fieldSystem);

#endif // POKEPLATINUM_FIELD_BOTTOM_SCREEN_H
