**MLB The Show 26 for PC 0.1.4: player models fixed on Intel Arc, smoother batting scenes, no developer tools running
in the background.**

> [!IMPORTANT]
> **No game files are included.** You need your own copy of MLB The Show 26 with the **1.0.20** update (the 1.0.15
> update still works), dumped as NSP files from **your own** Nintendo Switch, and your Switch's `prod.keys`. This
> download contains no game code, data or keys, and no tools to obtain them. The setup builds the game on your PC,
> from your files, for your own use: don't share the installed folder. This is a fan project, not affiliated with or
> endorsed by Sony Interactive Entertainment, San Diego Studio, MLB or Nintendo.

## What's new

- **Player models fixed on Intel Arc graphics cards** (A770, B580 and others, [#7](https://github.com/sam110411/mlbts-native/issues/7)).
  Pitchers drew without a torso in close-ups, and a huge grey shape covered part of the screen in the pitching view.
  The port checked where the game keeps its animated body data against a stricter rule than Intel's driver actually
  needs, and handed the game empty data instead. It now uses the driver's real rule.
- **Faster batting scenes.** At the plate, in the Road to the Show screens between pitches and at each batter's
  walk-up, the port spent too long preparing each frame for the graphics card: much of that time went to bookkeeping
  for the game's small data updates, which grew much faster than their number. That work is now much cheaper, so
  these scenes run faster, and the pause at each walk-up is shorter. How much faster depends on your processor.
- **No developer tools running while you play.** Earlier versions logged timing every second, sampled every thread
  for a profiler, and reacted to two debugging keys. All of that is now off:
  - **F12** no longer saves debug images of the frame (players pressed it for a screenshot and got a long freeze).
  - **F8** no longer writes a 42 MB memory dump with a beep.
  - The per-second timing lines are gone from `logs\mlb.log`; it still records errors and a frame-time summary
    every minute.

## Updating

Start `MLBTS-Setup.exe` (or wait for its launcher screen) and press **Download and update**. It rebuilds the game in a
few minutes and keeps your game files, saves and settings.

From 0.1.0: download `MLBTS-Setup-0.1.4-windows.zip`, unpack it, start **`MLBTS-Setup.exe`** and press **Update**.
You can delete the old setup folder afterwards.

## Download

| File | For |
|---|---|
| `MLBTS-Setup-0.1.4-windows.zip` | Windows 10 or 11, 64-bit |

## New install

1. Unpack the zip anywhere and start **`MLBTS-Setup.exe`**. Windows may warn that the app is unrecognised (it is not
   signed): choose **More info**, then **Run anyway**.
2. The setup looks for your keys and game files by itself: Ryujinx's and yuzu's folders, their game folders,
   Downloads, Desktop. Check what it found, pick what is missing, and press **Install**.
3. It extracts the game's data (about 38 GB), translates the game's code and compiles it for your PC. That takes about
   10 minutes; you can cancel and continue later.
4. Press **Play**. From then on, use the **MLB The Show 26** shortcut on your desktop.

## What you need

- Windows 10 or 11, 64-bit; a CPU with AVX2 (Intel 4th generation / AMD Ryzen or newer)
- A graphics card with Vulkan 1.3 and a current driver; DLSS needs an NVIDIA RTX card
- 16 GB of RAM; about 40 GB of free disk space, plus 2 GB next to the setup while it builds

## Known issues

- **Busy scenes can still drop below 60 fps on slower processors**, batting views especially: the port's work per
  frame there is lower now, but not yet low enough everywhere.
- **The first matches stutter** while the game prepares its shaders; they are kept, so it gets smoother as you play.
- Some images and lighting are still wrong or missing in places.
- The setup takes NSP files only (no XCI or NSZ yet), and only game versions 1.0.20 and 1.0.15.

## Reporting a problem

Open an issue and attach `logs\mlb.log` from the install folder (it is replaced each time the game starts, so copy it
right after the problem). For a setup problem, attach the newest `logs\setup-*.log` from the setup folder.
