#ifndef POKEPLATINUM_FIELD_INTERACTION_H
#define POKEPLATINUM_FIELD_INTERACTION_H

#include "struct_decls/map_object.h"

#include "field/field_system_decl.h"

#include "map_header_data.h"

void FieldEvent_FindMapObjectInFront(FieldSystem *fieldSystem, MapObject **param1);
u8 FieldEvent_TryGetInteractedMapObject(FieldSystem *fieldSystem, MapObject **param1);
u16 FieldEvent_GetInteractedBgEventScript(FieldSystem *fieldSystem, const BgEvent *bgEvents, int numBgEvents);
u16 FieldEvent_GetInteractedWallSignScript(FieldSystem *fieldSystem, const BgEvent *bgEvents, int numBgEvents);
u8 FieldEvent_TryGetInteractedSignpost(FieldSystem *fieldSystem, MapObject **param1);
u16 FieldEvent_GetTriggeredCoordEventScript(FieldSystem *fieldSystem, void *param1, int param2);

#endif // POKEPLATINUM_FIELD_INTERACTION_H
