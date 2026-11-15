**MLB The Show 26 for PC 0.1.2: renaming a player no longer crashes the game.**

> [!IMPORTANT]
> **No game files are included.** You need your own copy of MLB The Show 26 with the **1.0.15** update, dumped as NSP
> files from **your own** Nintendo Switch, and your Switch's `prod.keys`. This download contains no game code, data or
> keys, and no tools to obtain them. The setup builds the game on your PC, from your files, for your own use: don't
> share the installed folder. This is a fan project, not affiliated with or endorsed by Sony Interactive Entertainment,
> San Diego Studio, MLB or Nintendo.

## What's new

- **Changing a player's name works.** After a name was typed and confirmed (for example in Road to the Show's player
  editor), the game checked it with the console's word filter, which the port did not answer yet, and closed. The
  name is now accepted as typed.

## Updating

- **From 0.1.1:** start `MLBTS-Setup.exe` (or wait for its launcher screen) and press **Download and update**. It
  downloads this version, rebuilds the game in a few minutes and keeps your game files, saves and settings.
- **From 0.1.0:** download `MLBTS-Setup-0.1.2-windows.zip`, unpack it, start **`MLBTS-Setup.exe`** and press
  **Update**. From then on the launcher updates for you.

You can delete the old setup folder afterwards.

## Download

| File | For |
|---|---|
| `MLBTS-Setup-0.1.2-windows.zip` | Windows 10 or 11, 64-bit |

## New install

1. Unpack the zip anywhere and start **`MLBTS-Setup.exe`**. Windows may warn that the app is unrecognised (it is not
   signed): choose **More info**, then **Run anyway**.
2. The setup looks for your keys and game files by itself: Ryujinx's and yuzu's folders, their game folders,
   Downloads, Desktop. Check what it found, pick what is missing, and press **Install**.
3. It extracts the game's data (about 37 GB), translates the game's code and compiles it for your PC. That takes about
   10 minutes; you can cancel and continue later.
4. Press **Play**. From then on, use the **MLB The Show 26** shortcut on your desktop.

## What you need

- Windows 10 or 11, 64-bit; a CPU with AVX2 (Intel 4th generation / AMD Ryzen or newer)
- A graphics card with Vulkan 1.3 and a current driver; DLSS needs an NVIDIA RTX card
- 16 GB of RAM; about 40 GB of free disk space, plus 2 GB next to the setup while it builds

## Known issues

- **The first matches stutter** while the game prepares its shaders; they are kept, so it gets smoother as you play.
- Some images and lighting are still wrong or missing in places.
- The setup takes NSP files only (no XCI or NSZ yet), and only game version 1.0.15.

## Reporting a problem

Open an issue and attach `logs\mlb.log` from the install folder (it is replaced each time the game starts, so copy it
right after the problem). For a setup problem, attach the newest `logs\setup-*.log` from the setup folder.
