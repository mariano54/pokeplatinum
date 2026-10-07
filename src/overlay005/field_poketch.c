#include "overlay005/field_poketch.h"

#include <nitro.h>
#include <string.h>

#include "applications/poketch/unavailable/graphics.h"
#include "field/field_system.h"
#include "field/field_system_sub2_t.h"

#include "game_overlay.h"
#include "poketch.h"
#include "render_oam.h"
#include "system_flags.h"
#include "vars_flags.h"

FS_EXTERN_OVERLAY(poketch_unavailable);
FS_EXTERN_OVERLAY(poketch);

void FieldSystem_SendPoketchEvent(FieldSystem *fieldSystem, enum PoketchEventID eventID, u32 dummy)
{
    if (fieldSystem->fieldMapSubsystems != NULL && fieldSystem->fieldMapSubsystems->poketchSys != NULL) {
        PoketchSystem_SendEvent(fieldSystem->fieldMapSubsystems->poketchSys, eventID, dummy);
    }
}

void FieldPoketch_InitScreen(FieldSystem *fieldSystem)
{
    Poketch *poketch = SaveData_GetPoketch(fieldSystem->saveData);
    VarsFlags *varsFlags = SaveData_GetVarsFlags(fieldSystem->saveData);

    if (Poketch_IsEnabled(poketch)
        && (SystemFlag_CheckPoketchHidden(varsFlags) == 0)) {
        Overlay_LoadByID(FS_OVERLAY_ID(poketch), 2);
        PoketchSystem_Create(fieldSystem, &fieldSystem->fieldMapSubsystems->poketchSys, fieldSystem->saveData, fieldSystem->bgConfig, RenderOam_GetScreenOam(1));
    } else {
        Overlay_LoadByID(FS_OVERLAY_ID(poketch_unavailable), 2);
        PoketchUnavailableScreen_Init(fieldSystem->bgConfig);
    }
}

void FieldPoketch_EndScreen(FieldSystem *fieldSystem)
{
    Poketch *poketch = SaveData_GetPoketch(fieldSystem->saveData);
    VarsFlags *varsFlags = SaveData_GetVarsFlags(fieldSystem->saveData);

    if (Poketch_IsEnabled(poketch)
        && (SystemFlag_CheckPoketchHidden(varsFlags) == 0)) {
        PoketchSystem_StartShutdown(fieldSystem->fieldMapSubsystems->poketchSys);
    } else {
        PoketchUnavailableScreen_Exit(fieldSystem->bgConfig);
    }
}

u8 FieldPoketch_IsScreenDone(FieldSystem *fieldSystem)
{
    Poketch *poketch = SaveData_GetPoketch(fieldSystem->saveData);
    VarsFlags *varsFlags = SaveData_GetVarsFlags(fieldSystem->saveData);

    if (Poketch_IsEnabled(poketch)
        && (SystemFlag_CheckPoketchHidden(varsFlags) == 0)) {
        if (PoketchSystem_IsSystemShutdown(fieldSystem->fieldMapSubsystems->poketchSys)) {
            fieldSystem->fieldMapSubsystems->poketchSys = NULL;
            Overlay_UnloadByID(FS_OVERLAY_ID(poketch));
            return 1;
        }
    } else {
        if (PoketchUnavailableScreen_IsDone(fieldSystem->bgConfig)) {
            Overlay_UnloadByID(FS_OVERLAY_ID(poketch_unavailable));
            return 1;
        }
    }

    return 0;
}

void FieldPoketch_InitUnavailableScreen(FieldSystem *fieldSystem)
{
    Overlay_LoadByID(FS_OVERLAY_ID(poketch_unavailable), 2);
    PoketchUnavailableScreen_Init(fieldSystem->bgConfig);
}

void FieldPoketch_EndUnavailableScreen(FieldSystem *fieldSystem)
{
    PoketchUnavailableScreen_Exit(fieldSystem->bgConfig);
}

BOOL FieldPoketch_IsUnavailableScreenDone(FieldSystem *fieldSystem)
{
    if (PoketchUnavailableScreen_IsDone(fieldSystem->bgConfig)) {
        Overlay_UnloadByID(FS_OVERLAY_ID(poketch_unavailable));
        return 1;
    }

    return 0;
}
