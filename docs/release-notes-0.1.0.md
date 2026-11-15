**MLB The Show 26 for PC 0.1.0: the first release.**

> [!IMPORTANT]
> **No game files are included.** You need your own copy of MLB The Show 26 with the **1.0.15** update, dumped as NSP
> files from **your own** Nintendo Switch, and your Switch's `prod.keys`. This download contains no game code, data or
> keys, and no tools to obtain them. The setup builds the game on your PC, from your files, for your own use: don't
> share the installed folder. This is a fan project, not affiliated with or endorsed by Sony Interactive Entertainment,
> San Diego Studio, MLB or Nintendo.

## Download

| File | For |
|---|---|
| `MLBTS-Setup-0.1.0-windows.zip` | Windows 10 or 11, 64-bit |

## Install

1. Unpack the zip anywhere and start **`MLBTS-Setup.exe`**. Windows may warn that the app is unrecognised (it is not
   signed): choose **More info**, then **Run anyway**.
2. The setup looks for your keys and game files by itself: Ryujinx's and yuzu's folders, their game folders,
   Downloads, Desktop. Check what it found, pick what is missing, and press **Install**.
3. It extracts the game's data (about 37 GB), translates the game's code and compiles it for your PC. That takes about
   10 minutes; you can cancel and continue later.
4. Press **Play**. From then on, use the **MLB The Show 26** shortcut on your desktop.

If you played on Ryujinx, the setup can copy your save (Road to the Show, Franchise, settings) into the PC version.

## What you need

- Windows 10 or 11, 64-bit; a CPU with AVX2 (Intel 4th generation / AMD Ryzen or newer)
- A graphics card with Vulkan 1.3 and a current driver; DLSS needs an NVIDIA RTX card
- 16 GB of RAM; about 40 GB of free disk space, plus 2 GB next to the setup while it builds

## What works

Offline play: exhibition, Road to the Show, Franchise, menus and local saves, at 60 fps on the development PC (Ryzen 5
5600X, RTX 3060 Ti). Xbox and PlayStation controllers with rumble (the button prompts follow your controller), keyboard
and mouse. Settings in the game with **F1**: resolution and render scale, FSR 1, DLSS / DLAA, FXAA, shadows, HDR, frame
limit and more. **F11** switches full screen.

Online play and Diamond Dynasty are not supported.

## Known issues

- **The first matches stutter** while the game prepares its shaders; they are kept, so it gets smoother as you play.
- Some images and lighting are still wrong or missing in places.
- The setup takes NSP files only (no XCI or NSZ yet), and only game version 1.0.15.

## Reporting a problem

Open an issue and attach `logs\mlb.log` from the install folder (it is replaced each time the game starts, so copy it
right after the problem). For a setup problem, attach the newest `logs\setup-*.log` from the setup folder.
