**MLB The Show 26 for PC 0.1.3: runs game version 1.0.20, the latest update.**

> [!IMPORTANT]
> **No game files are included.** You need your own copy of MLB The Show 26 with the **1.0.20** update (the 1.0.15
> update still works), dumped as NSP files from **your own** Nintendo Switch, and your Switch's `prod.keys`. This
> download contains no game code, data or keys, and no tools to obtain them. The setup builds the game on your PC,
> from your files, for your own use: don't share the installed folder. This is a fan project, not affiliated with or
> endorsed by Sony Interactive Entertainment, San Diego Studio, MLB or Nintendo.

## What's new

- **Game version 1.0.20 is supported**, with its updated rosters, player faces and stadiums. Version 1.0.15 keeps
  working: the setup builds the version of the update file you give it, and prefers 1.0.20 when it finds both.
- In 1.0.20 the game-modes menu tries to reach the online store. The port answers that there is no network, and the
  game carries on (online features stay out of scope).

## Updating

- **Staying on 1.0.15:** start `MLBTS-Setup.exe` (or wait for its launcher screen) and press **Download and update**.
  It rebuilds the game in a few minutes and keeps your game files, saves and settings.
- **Moving to 1.0.20:** after updating the setup, choose **Install somewhere else**, pick the **1.0.20 update** NSP,
  and pick **your current install folder**. The setup replaces the old version's game data (about 38 GB, roughly 10
  minutes) and rebuilds; your saves and settings stay. Your 1.0.15 save loads in 1.0.20, but once 1.0.20 has saved
  over it, 1.0.15 may no longer read it.
- **From 0.1.0:** download `MLBTS-Setup-0.1.3-windows.zip`, unpack it, start **`MLBTS-Setup.exe`** and press
  **Update** (or install 1.0.20 as above).

You can delete the old setup folder afterwards.

## Download

| File | For |
|---|---|
| `MLBTS-Setup-0.1.3-windows.zip` | Windows 10 or 11, 64-bit |

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

- **The first matches stutter** while the game prepares its shaders; they are kept, so it gets smoother as you play.
  On 1.0.20 many shaders are new, so this happens again once.
- Some images and lighting are still wrong or missing in places.
- The setup takes NSP files only (no XCI or NSZ yet), and only game versions 1.0.20 and 1.0.15.

## Reporting a problem

Open an issue and attach `logs\mlb.log` from the install folder (it is replaced each time the game starts, so copy it
right after the problem). For a setup problem, attach the newest `logs\setup-*.log` from the setup folder.
