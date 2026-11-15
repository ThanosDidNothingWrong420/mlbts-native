**MLB The Show 26 for PC 0.1.1: typing works, and the setup can update itself.**

> [!IMPORTANT]
> **No game files are included.** You need your own copy of MLB The Show 26 with the **1.0.15** update, dumped as NSP
> files from **your own** Nintendo Switch, and your Switch's `prod.keys`. This download contains no game code, data or
> keys, and no tools to obtain them. The setup builds the game on your PC, from your files, for your own use: don't
> share the installed folder. This is a fan project, not affiliated with or endorsed by Sony Interactive Entertainment,
> San Diego Studio, MLB or Nintendo.

## What's new

- **Typing no longer freezes the game.** Wherever the game asks for text (for example the Road to the Show player
  search), a small "Enter text" box now opens over the game. Type and press **Enter** (or **OK**); **Esc** cancels. On a
  controller, **A** confirms and **B** cancels.
- **Updates from the launcher.** Starting `MLBTS-Setup.exe` again checks GitHub for a newer version. When there is one,
  press **Download and update**: the new setup is downloaded, checked and unpacked next to the old one, and it rebuilds
  the game right away. Your game files, saves and settings stay.

## Updating from 0.1.0

The 0.1.0 setup has no updater, so this one time: download `MLBTS-Setup-0.1.1-windows.zip`, unpack it, start
**`MLBTS-Setup.exe`** and press **Update**. It rebuilds the game in a few minutes; the game files are not needed again.
From 0.1.1 on, the launcher does this for you. You can delete the old setup folder afterwards.

## Download

| File | For |
|---|---|
| `MLBTS-Setup-0.1.1-windows.zip` | Windows 10 or 11, 64-bit |

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
