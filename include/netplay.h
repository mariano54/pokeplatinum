#ifndef POKEPLATINUM_NETPLAY_H
#define POKEPLATINUM_NETPLAY_H

#include <nitro.h>

#include "constants/heap.h"
#include "constants/string.h"

#include "struct_defs/pokemon.h"

#include "field/field_system_decl.h"

#include "charcode.h"
#include "field_script_context.h"
#include "party.h"
#include "pokemon.h"
#include "savedata.h"

// Experimental "netplay": two copies of the game running in emulators see each
// other in the overworld, and can battle or trade. The game only talks to a
// mailbox in RAM (gNetPlay); tools/netplay/bridge.py reads and writes it
// through the emulator's GDB stub and relays it between the two games.

#define NETPLAY_MAGIC 0x594C504E // "NPLY"
#define NETPLAY_VERSION 2

// Local ID given to the map object showing the other player.
#define NETPLAY_REMOTE_LOCAL_ID 0xF0

enum NetPlayRequest {
    NETPLAY_REQUEST_NONE = 0,
    NETPLAY_REQUEST_BATTLE,
    NETPLAY_REQUEST_TRADE,
    NETPLAY_REQUEST_CANCEL,
};

enum NetPlayResponse {
    NETPLAY_RESPONSE_NONE = 0,
    NETPLAY_RESPONSE_ACCEPT,
    NETPLAY_RESPONSE_DECLINE,
};

// How many of a player's latest steps are kept, so the other game can replay
// every step even if it only hears about them a few at a time.
#define NETPLAY_STEP_HISTORY 8

typedef struct NetPlayStep {
    s16 x; // where the step ends
    s16 z;
    u16 frame; // gNetPlay.frame when the step started, to replay steps with the same rhythm
    u8 action; // the movement action used, turned to face north (e.g. MOVEMENT_ACTION_RUN_NORTH)
    u8 dir; // the direction of the step
} NetPlayStep;

typedef struct NetPlayPlayer {
    u16 mapHeaderID;
    u16 mapMatrixID;
    s16 x;
    s16 z;
    u8 dir;
    u8 gender;
    u8 inField;
    u8 unused;
    u16 graphicsID; // the player's current overworld sprite: walking, cycling, surfing...
    u16 stepCount; // steps taken so far; step n is in steps[n % NETPLAY_STEP_HISTORY]
    NetPlayStep steps[NETPLAY_STEP_HISTORY];
    charcode_t name[TRAINER_NAME_LEN + 1];
} NetPlayPlayer;

typedef struct NetPlayState {
    u32 magic;
    u32 version;
    u32 frame;
    u16 injectKeys; // written by the bridge, ORed into the keypad...
    u8 remoteConnected;
    u8 injectFrames; // ...for this many more frames

    NetPlayPlayer local; // written by the game
    NetPlayPlayer remote; // written by the bridge

    // A request we sent (e.g. "battle?") and the answer to it
    u8 outRequest; // set by the game, cleared by the bridge once sent
    u8 inResponse; // set by the bridge
    // A request the other player sent us and our answer to it
    u8 inRequest; // set by the bridge, cleared by the game once shown
    u8 outResponse; // set by the game, cleared by the bridge once sent

    u8 tradeSlot; // the party slot we put up for trade
    // How smoothly the other player's steps play back: steps started later than
    // planned (so there was a pause before them), and the worst delay in frames.
    u8 replayLateSteps;
    u8 replayMaxLate;
    u8 pad;

    Pokemon outMon; // our Pokémon offered in a trade
    Pokemon inMon; // their Pokémon
    Party outParty; // our party, sent when a battle is accepted
    Party inParty; // their party
} NetPlayState;

// Script IDs of the netplay scripts (in res/field/scripts/scripts_unused_0397.s).
#define SCRIPT_ID_NETPLAY_TALK 9908
#define SCRIPT_ID_NETPLAY_INCOMING 9909

extern NetPlayState gNetPlay;

void NetPlay_Init(void);
u16 NetPlay_GetInjectedKeys(void);
BOOL NetPlay_FieldUpdate(FieldSystem *fieldSystem, BOOL playerHasControl);

void NetPlay_QuickStartTrainer(SaveData *saveData);
void NetPlay_QuickStartGame(SaveData *saveData, enum HeapID heapID);

BOOL ScrCmd_NetPlayBufferRemoteName(ScriptContext *ctx);
BOOL ScrCmd_NetPlayBufferTradeMons(ScriptContext *ctx);
BOOL ScrCmd_NetPlayBufferIncomingMon(ScriptContext *ctx);
BOOL ScrCmd_NetPlaySendRequest(ScriptContext *ctx);
BOOL ScrCmd_NetPlayWaitForResponse(ScriptContext *ctx);
BOOL ScrCmd_NetPlayGetIncomingRequest(ScriptContext *ctx);
BOOL ScrCmd_NetPlayRespond(ScriptContext *ctx);
BOOL ScrCmd_NetPlayCompleteTrade(ScriptContext *ctx);
BOOL ScrCmd_NetPlayStartBattle(ScriptContext *ctx);

#endif // POKEPLATINUM_NETPLAY_H
