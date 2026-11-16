# zeal-xonix

Short description of the project

## Requirements

Make sure that you have [ZDE](https://github.com/zoul0813/zeal-dev-environment) installed.

Install the ZGDK dependency and its required libraries once:

```sh
zde deps install zgdk
```

## Build

From this project directory, run:

```sh
zde cmake
```

## Run

From the Zeal 8-bit OS shell, on the disk that holds the binary:

```sh
./zeal-xonix.bin --level 12
```

`--level` accepts levels `1..21` (default is level 1).  The HUD shows the
current level, remaining time, captured percentage and score.

## Splash screen

The optional intro splash is loaded at runtime from:

```
assets/splash.zts
assets/splash.ztp
assets/splash.ztm
```

next to the binary.  `zde cmake` copies ready-made splash files from the
project's `assets/` directory into `bin/assets/`.  If the files are present,
the splash is shown with a fade-in; press any key to continue, or wait 25
seconds.  It then fades out and level 1 starts.  If the files are missing, a
warning is printed on the text console and the game starts without the splash.
`--level N` bypasses the splash, which is convenient for level testing.

## High scores

When the player runs out of lives, the game shows the top ten results.  If the
score qualifies (it is greater than zero and either the table has fewer than
ten entries or the score beats the lowest one), the player is asked for a name:

* `A-Z`, `0-9` and spaces are accepted, up to 8 characters.
* `Enter` confirms; `Backspace` deletes the last character.
* An empty name is stored as `PLAYER`.

The table is saved as `HISCORE.DAT` in the current directory, i.e. next to the
game binary when it is started from its own folder.  The file starts with the
`XHS1` magic, the records are XOR-obfuscated and protected by a 16-bit
checksum.  A missing, truncated or corrupted file is treated as an empty
table.

After the table is shown, press any key or wait 25 seconds.  The program then
returns to the beginning: the splash is shown again (unless `--level` was
used) and the game restarts at the selected start level.

## Sound effects

The game synthesizes its effects with the Zeal VideoBoard's four sound voices;
no external audio files are needed:

* balls bounce with a short, alternating ping-pong blip;
* closing a captured area plays a two-note bell;
* levels with spiders have a continuous low buzz while the spiders are active;
* losing one life plays a short descending "uh-oh" jingle;
* losing the last life plays a longer descending "oops" melody;
* a new number-one result plays a short victory fanfare on the table screen.

Gameplay effects stop before the high-score table.  The victory fanfare plays
only when the new result reaches first place.  Sound is also stopped when the
program restarts or exits.

## Capture performance

Territory capture uses a scanline span fill.  Whole open and already-visited
bytes are skipped in one pass, the trail is tracked byte-by-byte instead of
scanning its full bounding box, and fully filled bytes are written directly.
This shortens the pause after a large capture.

The between-level prompt also arms itself after one second even if a key is
held, and advances automatically after 30 seconds, so a stuck key cannot leave
the game waiting forever.

## Create Another Project

To create another ZGDK project from the same template, run:

```sh
zde create zgdk --name my-game
```
