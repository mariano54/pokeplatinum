# Netplay experiment

Two copies of the game, each in its own emulator, share an overworld through a small
Python server: each player sees the other walking around, with their name, and can
talk to them to battle or trade.

This lives on the `experiment/multiplayer` branch. It changes game code, so the ROM no
longer matches the original.

## Running it

Needs melonDS 1.1 (it has the GDB stub we use) and `arm-none-eabi-nm`. Stock melonDS
works for playing; the patched build adds what fast testing needs (savestates and speed
changes driven by the bridge, and JIT alongside the GDB stub). On macOS with Homebrew:

```sh
tools/netplay/melonds/build_melonds.sh    # -> build/melonds-netplay/melonDS.app (once)
make rom                                  # or ~/Projects/NDS/mk rom
python3 tools/netplay/play.py --melonds build/melonds-netplay/melonDS.app
```

`play.py` starts the relay server, two emulators and one bridge per emulator. Each
emulator is a portable copy of melonDS in `build/netplay/playerN`, with its own ROM
copy and save file. Player 1 is LUCAS and player 2 is DAWN; change that with `--names`.
Ctrl+C stops everything.

The emulators are made to stay out of the way. They're muted. On macOS,
`background_shim.m` is loaded into them: they keep drawing while covered by other
windows, and they don't take the keyboard when they start (click a window to play;
`--no-background-shim` turns this off).

In each game, choose NEW GAME. The intro is skipped: the player is named after the
emulator's firmware name and starts in Twinleaf Town with three Pokémon, running shoes
(hold B) and a Bicycle (press Y). Lucas's team is Turtwig, Starly and Shinx; Dawn's is
Piplup, Budew and Bidoof.

- **See:** the other player appears wherever they are in the same map, or anywhere on
  the outside world. When they come into view, the location banner says "DAWN is here!".
  They walk, run and cycle like the real player: every step is replayed with the same
  movement and sprite, at the same rhythm.
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
  sprite and name there, plus its last 8 steps: where each one ended, the movement
  action used (walk, run, cycle, ledge jump) and the frame it started on. It reads the
  other player's state from the mailbox and moves a map object (local ID `0xF0`) to
  match.
- **Smooth movement:** the bridges only see the games every 30 ms or so, so steps arrive
  in uneven bursts. The avatar therefore replays the other player's steps 4 frames
  behind, on the rhythm they were taken, rather than as soon as they arrive.
  `replayLateSteps` in the bridge status counts steps that still started late (none did
  in testing at 1x and 4x speed; see below for faster). If the avatar falls more than
  two steps behind, it catches up one speed faster.
- **Requests:** a request or answer is a byte in the mailbox. The Pokémon or party that
  goes with it is copied alongside.
- **Scripts:** the dialogue lives in `res/field/scripts/scripts_unused_0397.s` (script
  IDs 9908 talk and 9909 incoming request), using nine new `NetPlay*` script commands.
  `Encounter_NewVsNetPlay` starts the battle against the received party.
- **`bridge.py`:** pauses the emulator through melonDS's GDB stub about 30 times a
  second (`--poll-hz`), copies the mailbox to and from the server, and resumes. It
  finds `gNetPlay` in `build/main.nef`. It also listens on GDB port + 1000 for JSON
  commands, used to script the game for testing (see below).
- **`server.py`:** forwards JSON lines between the bridges. It could run on another
  machine; pass `--host 0.0.0.0` and point the bridges at it with `--server`.

## Testing quickly

- **Speed:** `play.py --speed 4` runs both games at 4x, `--speed 0` as fast as they go
  (about 600 frames/s each on an M-series Mac, 10x; `--jit` made little difference with
  two emulators running). With the patched melonDS, `drive.py 4333 speed 0` changes it
  while running. Booting into Twinleaf takes about 4 s at 4x. Above about 4x, the
  bridges' polling is too coarse for the replay margin, so the other player's steps can
  start a few frames late (`replayLateSteps`); positions still end up right.
- **Checkpoints:** with the patched melonDS, `checkpoint.py save NAME` saves both
  emulators' states at once (0.1 s), and `checkpoint.py load NAME`, or
  `play.py --load NAME` at startup, puts both games back exactly there. For example,
  from a checkpoint with the players side by side, a battle starts 2 s after loading.
  A savestate includes the running game code, so save checkpoints again after
  rebuilding the ROM; `checkpoint.py` refuses to load one saved with another build.
- **Scripting:** the bridge's control port (GDB port + 1000, so 4333 and 4433) takes JSON
  lines; `drive.py` wraps them. Times are in game frames, so a script does the same thing
  at any speed. (The overworld runs its logic every other screen refresh, so 30 game
  frames are one second at normal speed.)
  - `{"press": ["A"], "frames": 6, "wait": true}` holds buttons for 6 frames (the game
    applies them itself, so none are missed or held too long) and replies once they're
    released.
  - `{"wait": 60}` replies once the game has run 60 more frames.
  - `{"status": true}` returns the decoded mailbox, including `replayLateSteps`.
  - `{"savestate": PATH}`, `{"loadstate": PATH}` and `{"speed": N}` need the patched
    melonDS.

## Limits

- Two players. There are no collisions, by design.
- Battles are not real link battles: each side fights an AI copy of the other team,
  so the two battles can end differently.
- The other player is only drawn on the same map, or anywhere outside on the world map.
- The game state is still single-player. Story events (for example Mom's greeting
  after the skipped intro) run as usual.
