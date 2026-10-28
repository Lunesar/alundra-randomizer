![version](https://img.shields.io/badge/Version-1.2.0-blue)
![compile](https://github.com/Dinopony/alundra-randomizer/workflows/Compile/badge.svg)

# Alundra Randomizer

This project is a randomizer for the excellent action RPG Alundra, released on the first PlayStation in 1997.
It works on a 1.1 US image of the game by randomizing item sources and altering the game to make it more enjoyable in a randomizer format.

## Game flow

- Game starts inside Jess's house
- Most cutscenes and scenario-related blockers are removed to put the focus on gameplay & exploration
- The global goal is to find the 7 crests, open Lake Shrine using those and beat Melzas
- All arbitrary conditions from the original game tend to stay the same whenever relevant (e.g. Secret Pass to open the Casino)
- Keys (including Elevator Key) are not shuffled to keep the dungeon flow similar to the vanilla game

## Usage

### Release versions

Using the provided release packages (on the right panel of this webpage) is really straightforward since everything
(excepted the original ROM, of course) is included for you to start randomizing.

You just have to put your own copy of the original 1.1 US game disc inside the folder. The randomizer will
automatically pick it up if it uses one of these names:

- `input.bin`
- `Alundra.bin`
- `Alundra (USA).bin`
- `Alundra (USA) (Rev 1).bin`
- `Alundra (USA) (v1.1).bin`

Keeping a common dump name (instead of renaming to `input.bin` as older versions required) also keeps a matching `.cue`
file working. Use the `gen_preset.bat` script on Windows (Linux users are expected to know how to use CLI).

If your image uses a different filename, pass it with `--input=` when running the randomizer. Quote the path
if it contains spaces or parentheses (e.g. `--input="My Alundra dump.bin"`). On Windows, you can add that
argument to `gen_preset.bat` / `gen_permalink.bat`, or run `alundra-randomizer.exe` directly.

A command line interface will open, don't be scared! It will ask you for which **preset** you want to use. Presets are a
bunch of settings that define how the game will be randomized, and there is a default one provided with the randomizer.
These can be found under the `presets/` subfolder, and you can duplicate the existing ones and modify them to choose
your own settings if the default ones are not your cup of tea.
Back to the command line, you can just press Enter to use the default preset or you can type your own preset name before
confirming.

The randomizer will start working and generate a seed for you inside the `seeds/` subfolder, so you can play on your
favorite emulator / real console. It will also give you a **permalink**, which is a sort of code you can give your
randomizer pals so they can generate the exact same seed as you by using `gen_permalink.bat` and pasting this permalink.

Have fun randomizing!

### Splitting randomization and patching

The default command still randomizes and patches in one step. Two flags split that so a placement can be produced on one machine and applied on another (as is done for Archipelago worlds):

- `--placement-out=PATH` randomizes, writes a placement seed map JSON, and stops. It does not need a disc image and does not write the hint log. `PATH` is a `.json` file, or a directory in which the file is named after the hash sentence.
- `--placement-in=PATH` reads that JSON, generates hints from the placement, patches the disc image, and writes the spoiler/hint log (`--outputlog`).

`placements` in the file maps each location id to the in-game item id placed there. Location ids are the stable ids on item sources. The file also stores the permalink `seed`, `gameSettings`, and `randomizerSettings` that are needed to patch on another computer.

A Multiworld file (`"format": 2`) can also mark a location as another world's item (the sending player's name, the item name, a classification, and a shop price when that shelf sells it). The schema is in `src/io/placement_seed_map.hpp`. `--placement-out` still writes the Alundra format (`"format": 1`).

```
alundra-randomizer --preset=default --placement-out=./seeds/ --nopause
alundra-randomizer --placement-in=./seeds/placement.json --nopause
```

### Building from source

If you want to get the very last version, you can also compile and use the executable version on your own device. You
will need CMake to do so, and then do:

- On Windows, with Visual Studio 2019 installed, you can execute `build_win_vs2019.bat`
- On Unix, you can execute `build_unix.sh`

The compiled binary needs `dumpsxiso` and `mkpsxiso` under `tools/` (Windows: `tools\dumpsxiso.exe` and `tools\mkpsxiso.exe`).
Those tools are included in the release packages; if you build from source, get them from https://github.com/Lameguy64/mkpsxiso.
A missing tool or a non-zero exit is fatal. Tool output is printed on failure and written to `./tool.log` (kept on `--verbose` or if patching fails).
