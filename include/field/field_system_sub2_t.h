#ifndef POKEPLATINUM_FIELD_SYSTEM_SUB2_T_H
#define POKEPLATINUM_FIELD_SYSTEM_SUB2_T_H

#include "applications/poketch/poketch_system.h"
#include "overlay005/field_map_task_manager_decl.h"
#include "overlay005/hblank_system.h"
#include "overlay005/map_name_popup.h"
#include "overlay005/struct_ov5_021D5EF8_decl.h"
#include "overlay005/struct_ov5_021EF4F8_decl.h"
#include "overlay005/texture_resource_manager.h"

#include "berry_patch_manager.h"

struct FieldSystem_sub2_t {
    BOOL unused;
    FieldMapTaskManager *fieldMapTaskMan;
    MapNamePopUp *mapPopup;
    UnkStruct_ov5_021D5EF8 *weather;
    FieldTextureManager *mapTextureMan;
    PoketchSystem *poketchSys;
    BerryPatchManager *berryPatchManager;
    HBlankSystem *hBlankSystem;
    UnkStruct_ov5_021EF4F8 *poisonEffect;
    void *dynamicMapFeaturesData;
};

#endif // POKEPLATINUM_FIELD_SYSTEM_SUB2_T_H
