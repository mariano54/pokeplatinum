#include "macros/scrcmd.inc"
#include "res/text/bank/common_strings.h"
#include "res/text/bank/menu_entries.h"


    ScriptEntry Unused397_Dummy1
    ScriptEntry Unused397_Dummy2
    ScriptEntry Unused397_Dummy3
    ScriptEntry Unused397_Dummy4
    ScriptEntry Unused397_Dummy5
    ScriptEntry Unused397_Dummy6
    ScriptEntry Unused397_Dummy7
    ScriptEntry Unused397_Dummy8
    ScriptEntry NetPlay_TalkToPlayer
    ScriptEntry NetPlay_IncomingRequest
    ScriptEntryEnd

Unused397_Dummy1:
    SetFlag FLAG_HIDE_ROARK_UNDERGROUND
    SetVar VAR_HAS_SEEN_UNDERGROUND_ROARK_INTRO, 1
    End

Unused397_Dummy2:
    ClearFlag FLAG_HIDE_ROUTE_201_PROF_ROWAN
    ClearFlag FLAG_HIDE_ROUTE_201_COUNTERPART
    SetVar VAR_VISITED_LAKE_VERITY_WITH_RIVAL, 1
    SetVar VAR_VERITY_LAKEFRONT_STATE, 2
    ClearFlag FLAG_DUMMY_0x0193
    SetVar VAR_FOLLOWER_RIVAL_STATE, 2
    SetVar VAR_PLAYER_HOUSE_STATE, 3
    End

Unused397_Dummy3:
    SetVar VAR_SHAYMIN_EVENT_STATE, 1
    End

Unused397_Dummy4:
    ClearFlag FLAG_HIDE_LAKE_VERITY_LOW_WATER_COUNTERPART
    ClearFlag FLAG_HIDE_LAKE_VERITY_LOW_WATER_PROF_ROWAN
    SetVar VAR_VISITED_LAKE_VERITY_WITH_RIVAL, 1
    SetFlag FLAG_HIDE_LAKE_VERITY_LOW_WATER_RIVAL
    SetFlag FLAG_HIDE_VALOR_LAKEFRONT_CAMERAMEN
    ClearFlag FLAG_HIDE_LAKE_ACUITY_LOW_WATER_RIVAL
    End

Unused397_Dummy5:
    SetFlag FLAG_HIDE_POKECENTER_BASEMENT_BLOCKADE
    SetVar VAR_GTS_ACCESS_STATE, 1
    SetVar VAR_VISITED_POKEMON_CENTER_B1F, 1
    End

Unused397_Dummy6:
    End

Unused397_Dummy7:
    PlaySE SE_CONFIRM_sseq_3
    LockAll
    FacePlayer
    SetVar VAR_0x8005, 0
    GoTo Unused397_Unused1
    End

Unused397_Dummy8:
    PlaySE SE_CONFIRM_sseq_3
    LockAll
    FacePlayer
    SetVar VAR_0x8005, 1
    GoTo Unused397_Unused1
    End

Unused397_Unused1:
    CallIfEq VAR_0x8005, 0, Unused397_Unused15
    CallIfEq VAR_0x8005, 1, Unused397_Unused16
    CallIfEq VAR_0x8005, 2, Unused397_Unused17
    GoTo Unused397_Unused2
    End

Unused397_Unused2:
    CallIfEq VAR_0x8005, 0, Unused397_Unused18
    CallIfEq VAR_0x8005, 1, Unused397_Unused19
    CallIfEq VAR_0x8005, 2, Unused397_Unused20
    InitGlobalTextMenu 1, 1, 0, VAR_RESULT
    AddMenuEntryImm 14, 0
    AddMenuEntryImm 15, 1
    AddMenuEntryImm 16, 2
    ShowMenu
    GoToIfEq VAR_RESULT, 0, Unused397_Unused6
    GoToIfEq VAR_RESULT, 1, Unused397_Unused3
    GoTo Unused397_Unused4
    End

Unused397_Unused3:
    CallIfEq VAR_0x8005, 0, Unused397_Unused21
    CallIfEq VAR_0x8005, 1, Unused397_Unused22
    CallIfEq VAR_0x8005, 2, Unused397_Unused23
    GoTo Unused397_Unused2
    End

Unused397_Unused4:
    GoTo Unused397_Unused5
    End

Unused397_Unused5:
    Message 6
    WaitButton
    CloseMessage
    ReleaseAll
    End

Unused397_Unused6:
    Message 7
    InitGlobalTextMenu 1, 1, 0, VAR_RESULT
    AddMenuEntryImm 17, 0
    AddMenuEntryImm 18, 1
    AddMenuEntryImm 19, 2
    ShowMenu
    GoToIfEq VAR_RESULT, 0, Unused397_Unused7
    GoToIfEq VAR_RESULT, 1, Unused397_Unused8
    GoTo Unused397_Unused4
    End

Unused397_Unused7:
    SetVar VAR_0x8006, 0
    GoTo Unused397_Unused9
    End

Unused397_Unused8:
    SetVar VAR_0x8006, 1
    GoTo Unused397_Unused9
    End

Unused397_Unused9:
    Message 8
    ShowYesNoMenu VAR_RESULT
    GoToIfEq VAR_RESULT, MENU_NO, Unused397_Unused4
    GoTo Unused397_Unused10
    End

Unused397_Unused10:
    SetVar VAR_MAP_LOCAL_0x00, 0
    Common_SaveGame
    SetVar VAR_RESULT, VAR_MAP_LOCAL_0x00
    GoToIfEq VAR_RESULT, 0, Unused397_Unused4
    GoTo Unused397_Unused11
    End

Unused397_Unused11:
    Message 9
    CloseMessage
    CallIfEq VAR_0x8005, 0, Unused397_Unused12
    CallIfEq VAR_0x8005, 1, Unused397_Unused13
    CallIfEq VAR_0x8005, 2, Unused397_Unused14
    End

Unused397_Unused12:
    Return

Unused397_Unused13:
    Return

Unused397_Unused14:
    Return

Unused397_Unused15:
    Message 0
    Return

Unused397_Unused16:
    Return

Unused397_Unused17:
    Message 3
    Return

Unused397_Unused18:
    Message 1
    Return

Unused397_Unused19:
    Return

Unused397_Unused20:
    Message 4
    Return

Unused397_Unused21:
    Message 2
    Return

Unused397_Unused22:
    Return

Unused397_Unused23:
    Message 5
    Return

// Netplay (see include/netplay.h). Values of enum NetPlayRequest / NetPlayResponse:
.set NETPLAY_REQUEST_BATTLE, 1
.set NETPLAY_REQUEST_TRADE, 2
.set NETPLAY_RESPONSE_ACCEPT, 1

// Talking to the other player's map object.
NetPlay_TalkToPlayer:
    LockAll
    FacePlayer
    NetPlayBufferRemoteName 0
    Message CommonStrings_Text_NetPlayGreeting
    InitGlobalTextMenu 30, 1, 0, VAR_RESULT
    SetMenuXOriginToRight
    AddMenuEntryImm MenuEntries_Text_UnionRoom_Battle, 0
    AddMenuEntryImm MenuEntries_Text_UnionRoom_Trade, 1
    AddMenuEntryImm MenuEntries_Text_UnionRoom_Cancel, 2
    ShowMenu
    GoToIfEq VAR_RESULT, 0, NetPlay_ChallengeToBattle
    GoToIfEq VAR_RESULT, 1, NetPlay_OfferTrade
    GoTo NetPlay_End
    End

NetPlay_ChallengeToBattle:
    NetPlaySendRequest NETPLAY_REQUEST_BATTLE, 0
    Message CommonStrings_Text_NetPlayWaiting
    NetPlayWaitForResponse VAR_RESULT
    GoToIfNe VAR_RESULT, NETPLAY_RESPONSE_ACCEPT, NetPlay_Declined
    CloseMessage
    GoTo NetPlay_Battle
    End

NetPlay_OfferTrade:
    Message CommonStrings_Text_NetPlayChooseMonToOffer
    WaitABPress
    CloseMessage
    FadeScreenOut
    WaitFadeScreen
    SelectPokemonToTrade
    FadeScreenIn
    WaitFadeScreen
    GoToIfEq VAR_RESULT, PARTY_SLOT_NONE, NetPlay_End
    NetPlaySendRequest NETPLAY_REQUEST_TRADE, VAR_RESULT
    NetPlayBufferRemoteName 0
    Message CommonStrings_Text_NetPlayWaiting
    NetPlayWaitForResponse VAR_RESULT
    GoToIfNe VAR_RESULT, NETPLAY_RESPONSE_ACCEPT, NetPlay_Declined
    GoTo NetPlay_Trade
    End

NetPlay_Declined:
    NetPlayBufferRemoteName 0
    Message CommonStrings_Text_NetPlayDeclined
    WaitABPress
    GoTo NetPlay_End
    End

// The other player asked us to battle or trade.
NetPlay_IncomingRequest:
    LockAll
    NetPlayBufferRemoteName 0
    NetPlayGetIncomingRequest VAR_0x8004
    GoToIfEq VAR_0x8004, NETPLAY_REQUEST_TRADE, NetPlay_IncomingTrade
    Message CommonStrings_Text_NetPlayBattleChallenge
    ShowYesNoMenu VAR_RESULT
    GoToIfEq VAR_RESULT, MENU_NO, NetPlay_RefuseRequest
    CloseMessage
    NetPlayRespond TRUE, 0
    GoTo NetPlay_Battle
    End

NetPlay_IncomingTrade:
    NetPlayBufferIncomingMon 1
    Message CommonStrings_Text_NetPlayTradeOffer
    ShowYesNoMenu VAR_RESULT
    GoToIfEq VAR_RESULT, MENU_NO, NetPlay_RefuseRequest
    Message CommonStrings_Text_NetPlayChooseMonToSend
    WaitABPress
    CloseMessage
    FadeScreenOut
    WaitFadeScreen
    SelectPokemonToTrade
    FadeScreenIn
    WaitFadeScreen
    GoToIfEq VAR_RESULT, PARTY_SLOT_NONE, NetPlay_RefuseRequest
    NetPlayRespond TRUE, VAR_RESULT
    GoTo NetPlay_Trade
    End

NetPlay_RefuseRequest:
    NetPlayRespond FALSE, 0
    GoTo NetPlay_End
    End

// Both players battle the other's team, played by the AI.
NetPlay_Battle:
    NetPlayStartBattle
    HealParty
    NetPlayBufferRemoteName 0
    Message CommonStrings_Text_NetPlayBattleOver
    WaitABPress
    GoTo NetPlay_End
    End

NetPlay_Trade:
    NetPlayBufferTradeMons 1, 2
    NetPlayCompleteTrade
    PlayFanfare SEQ_FANFA1_sseq
    Message CommonStrings_Text_NetPlayTraded
    WaitFanfare
    WaitABPress
    GoTo NetPlay_End
    End

NetPlay_End:
    CloseMessage
    ReleaseAll
    End
