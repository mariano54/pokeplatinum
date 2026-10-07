# Netplay experiment

Two copies of the game, each in its own emulator, share an overworld through a small
Python server: each player sees the other walking around, with their name, and can
talk to them to battle or trade.

This lives on the `experiment/multiplayer` branch. It changes game code, so the ROM no
longer matches the original.

## Running it

Needs melonDS 1.1 or newer (it has the GDB stub we use) and `arm-none-eabi-nm`.

```sh
make rom    # or ~/Projects/NDS/mk rom
python3 tools/netplay/play.py --melonds /path/to/melonDS.app --always-render
```

`play.py` starts the relay server, two emulators and one bridge per emulator. Each
emulator is a portable copy of melonDS in `build/netplay/playerN`, with its own ROM
copy and save file. Both emulators are muted. Player 1 is LUCAS and player 2 is DAWN;
change that with `--names`. `--always-render` (macOS) keeps the games drawing while
their windows are covered by other windows. Ctrl+C stops everything.

In each game, choose NEW GAME. The intro is skipped: the player is named after the
emulator's firmware name and starts in Twinleaf Town with three Pokémon. Lucas's team is
Turtwig, Starly and Shinx; Dawn's is Piplup, Budew and Bidoof.

- **See:** the other player appears wherever they are in the same map, or anywhere on
  the outside world. When they come into view, the location banner says "DAWN is here!".
- **Talk:** walk up to them and press A for BATTLE / TRADE / CANCEL.
- **Battle:** the other player is asked to accept. Each game then battles the other
  player's team, played by the game's AI (the two battles aren't lockstepped). Whether
  you win or lose, your party is healed afterwards; there's no blackout.
- **Trade:** pick a Pokémon to offer. The other player sees what it is, accepts and
  picks one to send back, and both are swapped.

Requests wait while the other player is busy, for example in a battle or a menu.
B cancels a request while you wait.

## How it works

```
 melonDS #1 ──GDB── bridge.py ──┐                    ┌── bridge.py ──GDB── melonDS #2
 (gNetPlay)     read/write RAM  └── server.py (TCP) ─┘   read/write RAM    (gNetPlay)
```

- **In the game** (`include/netplay.h`, `src/netplay.c`): a mailbox struct `gNetPlay`
  in RAM. Every field frame the game writes its own position, map, facing direction,
  gender and name there. It reads the other player's state from the mailbox and moves
  a map object (local ID `0xF0`) to match.
- **Requests:** a request or answer is a byte in the mailbox. The Pokémon or party that
  goes with it is copied alongside.
- **Scripts:** the dialogue lives in `res/field/scripts/scripts_unused_0397.s` (script
  IDs 9908 talk and 9909 incoming request), using nine new `NetPlay*` script commands.
  `Encounter_NewVsNetPlay` starts the battle against the received party.
- **`bridge.py`:** pauses the emulator through melonDS's GDB stub about 15 times a
  second, copies the mailbox to and from the server, and resumes. It finds `gNetPlay`
  in `build/main.nef`. It also listens on GDB port + 1000 for JSON commands:
  `{"press": ["A"], "frames": 6}` holds buttons and `{"status": true}` returns the
  decoded mailbox. `drive.py` is a small CLI for that, used to script the game for
  testing.
- **`server.py`:** forwards JSON lines between the bridges. It could run on another
  machine; pass `--host 0.0.0.0` and point the bridges at it with `--server`.

## Limits

- Two players. There are no collisions, by design.
- Battles are not real link battles: each side fights an AI copy of the other team,
  so the two battles can end differently.
- The other player is only drawn on the same map, or anywhere outside on the world map.
- The game state is still single-player. Story events (for example Mom's greeting
  after the skipped intro) run as usual.
