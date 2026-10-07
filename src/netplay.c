#include "netplay.h"

#include <nitro.h>
#include <string.h>

#include "constants/heap.h"
#include "constants/map_object.h"
#include "constants/pokemon.h"
#include "constants/charcode.h"
#include "generated/genders.h"
#include "generated/items.h"
#include "generated/map_headers.h"
#include "generated/movement_actions.h"
#include "generated/movement_types.h"
#include "generated/object_events_gfx.h"
#include "generated/species.h"
#include "generated/vars_flags.h"

#include "field/field_system.h"
#include "field/field_system_sub2_t.h"
#include "overlay005/map_name_popup.h"

#include "charcode.h"
#include "charcode_util.h"
#include "encounter.h"
#include "field_overworld_state.h"
#include "field_script_context.h"
#include "inlines.h"
#include "location.h"
#include "map_header.h"
#include "map_object.h"
#include "message.h"
#include "party.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata.h"
#include "script_manager.h"
#include "string_gf.h"
#include "string_template.h"
#include "system.h"
#include "system_vars.h"
#include "trainer_info.h"
#include "unk_02054884.h"
#include "unk_020655F4.h"
#include "vars_flags.h"

#include "res/text/bank/common_strings.h"

// Places in the overworld matrix share one coordinate space, so players on
// different map headers there (e.g. Twinleaf Town and Route 201) still see
// each other.
#define OVERWORLD_MAP_MATRIX 0

NetPlayState gNetPlay;

static u16 sRemoteAvatarHeader;
static BOOL sRemoteVisible; // whether the other player has been announced since they came into view

static void NetPlay_EnsureInit(void)
{
    if (gNetPlay.magic != NETPLAY_MAGIC) {
        memset(&gNetPlay, 0, sizeof(gNetPlay));
        gNetPlay.magic = NETPLAY_MAGIC;
        gNetPlay.version = NETPLAY_VERSION;
    }
}

void NetPlay_Init(void)
{
    NetPlay_EnsureInit();
}

u16 NetPlay_GetInjectedKeys(void)
{
    NetPlay_EnsureInit();
    gNetPlay.frame++;
    return gNetPlay.injectKeys;
}

static void NetPlay_UpdateLocalPlayer(FieldSystem *fieldSystem)
{
    NetPlayPlayer *local = &gNetPlay.local;
    TrainerInfo *trainerInfo = SaveData_GetTrainerInfo(fieldSystem->saveData);
    u16 mapHeaderID = fieldSystem->location->mapHeaderID;
    s16 x = PlayerAvatar_GetXPos(fieldSystem->playerAvatar);
    s16 z = PlayerAvatar_GetZPos(fieldSystem->playerAvatar);
    u8 dir = PlayerAvatar_GetFacingDir(fieldSystem->playerAvatar);

    if (local->mapHeaderID != mapHeaderID || local->x != x || local->z != z || local->dir != dir || !local->inField) {
        local->mapHeaderID = mapHeaderID;
        local->mapMatrixID = MapHeader_GetMapMatrixID(mapHeaderID);
        local->x = x;
        local->z = z;
        local->dir = dir;
        local->inField = TRUE;
        local->seq++;
    }

    local->gender = TrainerInfo_Gender(trainerInfo);
    CharCode_Copy(local->name, TrainerInfo_Name(trainerInfo));
}

static BOOL NetPlay_IsRemoteVisible(FieldSystem *fieldSystem)
{
    const NetPlayPlayer *remote = &gNetPlay.remote;
    u16 mapHeaderID = fieldSystem->location->mapHeaderID;

    if (!gNetPlay.remoteConnected || !remote->inField) {
        return FALSE;
    }

    if (remote->mapHeaderID == mapHeaderID) {
        return TRUE;
    }

    return remote->mapMatrixID == OVERWORLD_MAP_MATRIX && MapHeader_GetMapMatrixID(mapHeaderID) == OVERWORLD_MAP_MATRIX;
}

static int NetPlay_DirTowards(int dx, int dz)
{
    if (dx > 0) {
        return DIR_EAST;
    } else if (dx < 0) {
        return DIR_WEST;
    } else if (dz > 0) {
        return DIR_SOUTH;
    }

    return DIR_NORTH;
}

// Shows "<name> is here!" in the location name banner. Returns FALSE if the banner is busy.
static BOOL NetPlay_AnnounceRemote(FieldSystem *fieldSystem)
{
    MessageLoader *loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG, TEXT_BANK_COMMON_STRINGS, HEAP_ID_FIELD1);
    StringTemplate *strTemplate = StringTemplate_Default(HEAP_ID_FIELD1);
    String *name = String_Init(TRAINER_NAME_LEN + 1, HEAP_ID_FIELD1);
    String *fmt = MessageLoader_GetNewString(loader, CommonStrings_Text_NetPlayNearby);
    String *text = String_Init(22, HEAP_ID_FIELD1);
    int windowID = MapHeader_GetMapLabelWindowID(fieldSystem->location->mapHeaderID);

    String_CopyChars(name, gNetPlay.remote.name);
    StringTemplate_SetString(strTemplate, 0, name, 0, TRUE, GAME_LANGUAGE);
    StringTemplate_Format(strTemplate, text, fmt);
    BOOL shown = MapNamePopUp_ShowText(fieldSystem->fieldMapSubsystems->mapPopup, text, windowID > 0 ? windowID - 1 : 0);

    String_Free(text);
    String_Free(fmt);
    String_Free(name);
    StringTemplate_Free(strTemplate);
    MessageLoader_Free(loader);
    return shown;
}

// Shows the other player as a map object that walks to wherever they are.
static void NetPlay_UpdateRemoteAvatar(FieldSystem *fieldSystem, BOOL playerHasControl)
{
    const NetPlayPlayer *remote = &gNetPlay.remote;
    MapObject *mapObj = MapObjMan_LocalMapObjByIndex(fieldSystem->mapObjMan, NETPLAY_REMOTE_LOCAL_ID);
    u16 mapHeaderID = fieldSystem->location->mapHeaderID;
    int graphicsID = remote->gender == GENDER_FEMALE ? OBJ_EVENT_GFX_PLAYER_F : OBJ_EVENT_GFX_PLAYER_M;

    if (!NetPlay_IsRemoteVisible(fieldSystem)) {
        if (mapObj != NULL) {
            MapObject_Delete(mapObj);
        }
        sRemoteVisible = FALSE;
        return;
    }

    // Announce the other player by name whenever they come into view. Waiting for the
    // player to have control lets the location name banner of a new map go first.
    if (!sRemoteVisible && playerHasControl) {
        sRemoteVisible = NetPlay_AnnounceRemote(fieldSystem);
    }

    if (mapObj != NULL && (sRemoteAvatarHeader != mapHeaderID || MapObject_GetGraphicsID(mapObj) != graphicsID)) {
        MapObject_Delete(mapObj);
        mapObj = NULL;
    }

    if (mapObj == NULL) {
        mapObj = MapObjectMan_AddMapObject(fieldSystem->mapObjMan, remote->x, remote->z, remote->dir, graphicsID, MOVEMENT_TYPE_NONE, mapHeaderID);

        if (mapObj != NULL) {
            MapObject_SetLocalID(mapObj, NETPLAY_REMOTE_LOCAL_ID);
            MapObject_SetScript(mapObj, SCRIPT_ID_NETPLAY_TALK);
            sRemoteAvatarHeader = mapHeaderID;
        }
        return;
    }

    if (!LocalMapObj_IsAnimationSet(mapObj)) {
        return; // still walking
    }

    int dx = remote->x - MapObject_GetX(mapObj);
    int dz = remote->z - MapObject_GetZ(mapObj);

    if (dx == 0 && dz == 0) {
        if (MapObject_GetFacingDir(mapObj) != remote->dir) {
            MapObject_Face(mapObj, remote->dir);
        }
    } else if (dx * dx + dz * dz == 1) {
        LocalMapObj_SetAnimationCode(mapObj, MovementAction_TurnActionTowardsDir(NetPlay_DirTowards(dx, dz), MOVEMENT_ACTION_WALK_FAST_NORTH));
    } else {
        // Too far behind (or warped): jump straight there.
        MapObject_SetPosDirFromCoords(mapObj, remote->x, MapObject_GetY(mapObj), remote->z, remote->dir);
    }
}

BOOL NetPlay_FieldUpdate(FieldSystem *fieldSystem, BOOL playerHasControl)
{
    NetPlay_EnsureInit();

    if (!fieldSystem->runningFieldMap || fieldSystem->playerAvatar == NULL || fieldSystem->mapObjMan == NULL) {
        return FALSE;
    }

    NetPlay_UpdateLocalPlayer(fieldSystem);
    NetPlay_UpdateRemoteAvatar(fieldSystem, playerHasControl);

    if (gNetPlay.inRequest == NETPLAY_REQUEST_CANCEL) {
        gNetPlay.inRequest = NETPLAY_REQUEST_NONE;
    }

    if (playerHasControl && gNetPlay.inRequest != NETPLAY_REQUEST_NONE) {
        ScriptManager_Set(fieldSystem, SCRIPT_ID_NETPLAY_INCOMING, NULL);
        return TRUE;
    }

    return FALSE;
}

// --- quick start ----------------------------------------------------------

static const u16 sQuickStartTeams[2][3][2] = {
    { { SPECIES_TURTWIG, 15 }, { SPECIES_STARLY, 13 }, { SPECIES_SHINX, 12 } },
    { { SPECIES_PIPLUP, 15 }, { SPECIES_BUDEW, 13 }, { SPECIES_BIDOOF, 12 } },
};

static charcode_t NetPlay_CharFromUnicode(u16 c)
{
    if (c >= 'A' && c <= 'Z') {
        return 0x12B + (c - 'A');
    } else if (c >= 'a' && c <= 'z') {
        return 0x145 + (c - 'a');
    } else if (c >= '0' && c <= '9') {
        return 0x121 + (c - '0');
    }

    return 0;
}

// New games skip Professor Rowan's intro: the player's name comes from the
// console's (emulator's) owner name, and an even birthday month makes them Dawn.
void NetPlay_QuickStartTrainer(SaveData *saveData)
{
    TrainerInfo *trainerInfo = SaveData_GetTrainerInfo(saveData);
    OSOwnerInfo owner;
    charcode_t name[TRAINER_NAME_LEN + 1];
    int len = 0;

    OS_GetOwnerInfo(&owner);

    for (int i = 0; i < owner.nickNameLength && len < TRAINER_NAME_LEN; i++) {
        charcode_t c = NetPlay_CharFromUnicode(owner.nickName[i]);

        if (c != 0) {
            name[len++] = c;
        }
    }

    if (len == 0) {
        name[len++] = NetPlay_CharFromUnicode('P');
        name[len++] = NetPlay_CharFromUnicode('L');
        name[len++] = NetPlay_CharFromUnicode('A');
        name[len++] = NetPlay_CharFromUnicode('Y');
        name[len++] = NetPlay_CharFromUnicode('E');
        name[len++] = NetPlay_CharFromUnicode('R');
    }

    name[len] = CHAR_EOS;
    TrainerInfo_SetName(trainerInfo, name);
    TrainerInfo_SetGender(trainerInfo, owner.birthday.month % 2 == 0 ? GENDER_FEMALE : GENDER_MALE);
}

void NetPlay_QuickStartGame(SaveData *saveData, enum HeapID heapID)
{
    TrainerInfo *trainerInfo = SaveData_GetTrainerInfo(saveData);
    int gender = TrainerInfo_Gender(trainerInfo);
    Location *location = FieldOverworldState_GetPlayerLocation(SaveData_GetFieldOverworldState(saveData));

    // Outside the player's house in Twinleaf Town; Dawn stands a little to the right.
    Location_Set(location, MAP_HEADER_TWINLEAF_TOWN, WARP_ID_NONE, gender == GENDER_FEMALE ? 118 : 115, 887, FACE_DOWN);

    for (int i = 0; i < 3; i++) {
        Pokemon_GiveMonFromScript(heapID, saveData, sQuickStartTeams[gender][i][0], sQuickStartTeams[gender][i][1], ITEM_NONE, 0, 0);
    }

    // The start menu only shows POKéMON once a starter has been chosen.
    SystemVars_SetPlayerStarter(SaveData_GetVarsFlags(saveData), sQuickStartTeams[gender][0][0]);

    // Skip the rival bumping into the player on their way out of town.
    *VarsFlags_GetVarAddress(SaveData_GetVarsFlags(saveData), VAR_TWINLEAF_TOWN_RIVAL_TRIGGER_STATE) = 1;
}

// --- script commands ------------------------------------------------------

static StringTemplate *NetPlay_GetStringTemplate(ScriptContext *ctx)
{
    StringTemplate **strTemplate = FieldSystem_GetScriptMemberPtr(ctx->fieldSystem, SCRIPT_MANAGER_STR_TEMPLATE);
    return *strTemplate;
}

BOOL ScrCmd_NetPlayBufferRemoteName(ScriptContext *ctx)
{
    u8 templateArg = ScriptContext_ReadByte(ctx);
    String *name = String_Init(TRAINER_NAME_LEN + 1, HEAP_ID_FIELD2);

    String_CopyChars(name, gNetPlay.remote.name);
    StringTemplate_SetString(NetPlay_GetStringTemplate(ctx), templateArg, name, 0, TRUE, GAME_LANGUAGE);
    String_Free(name);
    return FALSE;
}

// Buffers the species of the Pokémon we sent and the one we received.
BOOL ScrCmd_NetPlayBufferTradeMons(ScriptContext *ctx)
{
    u8 sentArg = ScriptContext_ReadByte(ctx);
    u8 receivedArg = ScriptContext_ReadByte(ctx);
    StringTemplate *strTemplate = NetPlay_GetStringTemplate(ctx);

    StringTemplate_SetSpeciesName(strTemplate, sentArg, Pokemon_GetBoxPokemon(&gNetPlay.outMon));
    StringTemplate_SetSpeciesName(strTemplate, receivedArg, Pokemon_GetBoxPokemon(&gNetPlay.inMon));
    return FALSE;
}

BOOL ScrCmd_NetPlayBufferIncomingMon(ScriptContext *ctx)
{
    u8 templateArg = ScriptContext_ReadByte(ctx);

    StringTemplate_SetSpeciesName(NetPlay_GetStringTemplate(ctx), templateArg, Pokemon_GetBoxPokemon(&gNetPlay.inMon));
    return FALSE;
}

static void NetPlay_OfferPartySlot(FieldSystem *fieldSystem, u8 request, u16 slot)
{
    Party *party = SaveData_GetParty(fieldSystem->saveData);

    if (request == NETPLAY_REQUEST_TRADE) {
        gNetPlay.tradeSlot = slot;
        memcpy(&gNetPlay.outMon, Party_GetPokemonBySlotIndex(party, slot), sizeof(Pokemon));
    } else if (request == NETPLAY_REQUEST_BATTLE) {
        Party_Copy(party, &gNetPlay.outParty);
    }
}

BOOL ScrCmd_NetPlaySendRequest(ScriptContext *ctx)
{
    u8 request = ScriptContext_ReadByte(ctx);
    u16 slot = ScriptContext_GetVar(ctx);

    NetPlay_OfferPartySlot(ctx->fieldSystem, request, slot);
    gNetPlay.inResponse = NETPLAY_RESPONSE_NONE;
    gNetPlay.outRequest = request;
    return FALSE;
}

static BOOL NetPlay_ShouldResumeAfterResponse(ScriptContext *ctx)
{
    u16 *destVar = FieldSystem_GetVarPointer(ctx->fieldSystem, ctx->data[0]);

    if (gNetPlay.inResponse != NETPLAY_RESPONSE_NONE) {
        *destVar = gNetPlay.inResponse;
        gNetPlay.inResponse = NETPLAY_RESPONSE_NONE;
        return TRUE;
    }

    if (gSystem.pressedKeys & PAD_BUTTON_B) {
        gNetPlay.outRequest = NETPLAY_REQUEST_CANCEL;
        *destVar = NETPLAY_RESPONSE_DECLINE;
        return TRUE;
    }

    return FALSE;
}

BOOL ScrCmd_NetPlayWaitForResponse(ScriptContext *ctx)
{
    ctx->data[0] = ScriptContext_ReadHalfWord(ctx);
    ScriptContext_Pause(ctx, NetPlay_ShouldResumeAfterResponse);
    return TRUE;
}

BOOL ScrCmd_NetPlayGetIncomingRequest(ScriptContext *ctx)
{
    u16 *destVar = ScriptContext_GetVarPointer(ctx);

    *destVar = gNetPlay.inRequest;
    return FALSE;
}

BOOL ScrCmd_NetPlayRespond(ScriptContext *ctx)
{
    u8 accept = ScriptContext_ReadByte(ctx);
    u16 slot = ScriptContext_GetVar(ctx);

    if (accept) {
        NetPlay_OfferPartySlot(ctx->fieldSystem, gNetPlay.inRequest, slot);
    }

    gNetPlay.outResponse = accept ? NETPLAY_RESPONSE_ACCEPT : NETPLAY_RESPONSE_DECLINE;
    gNetPlay.inRequest = NETPLAY_REQUEST_NONE;
    return FALSE;
}

BOOL ScrCmd_NetPlayCompleteTrade(ScriptContext *ctx)
{
    Party *party = SaveData_GetParty(ctx->fieldSystem->saveData);

    Party_AddPokemonBySlotIndex(party, gNetPlay.tradeSlot, &gNetPlay.inMon);
    return FALSE;
}

BOOL ScrCmd_NetPlayStartBattle(ScriptContext *ctx)
{
    int *battleResultMaskPtr = FieldSystem_GetScriptMemberPtr(ctx->fieldSystem, SCRIPT_MANAGER_BATTLE_RESULT);

    Encounter_NewVsNetPlay(ctx->task, &gNetPlay.inParty, gNetPlay.remote.name, gNetPlay.remote.gender, battleResultMaskPtr);
    return TRUE;
}
