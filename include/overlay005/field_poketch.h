#ifndef POKEPLATINUM_FIELD_POKETCH_H
#define POKEPLATINUM_FIELD_POKETCH_H

#include "applications/poketch/poketch_system.h"
#include "field/field_system_decl.h"

void FieldSystem_SendPoketchEvent(FieldSystem *fieldSystem, enum PoketchEventID eventID, u32 dummy);
void FieldPoketch_InitScreen(FieldSystem *fieldSystem);
void FieldPoketch_EndScreen(FieldSystem *fieldSystem);
u8 FieldPoketch_IsScreenDone(FieldSystem *fieldSystem);
void FieldPoketch_InitUnavailableScreen(FieldSystem *fieldSystem);
void FieldPoketch_EndUnavailableScreen(FieldSystem *fieldSystem);
BOOL FieldPoketch_IsUnavailableScreenDone(FieldSystem *fieldSystem);

#endif // POKEPLATINUM_FIELD_POKETCH_H
