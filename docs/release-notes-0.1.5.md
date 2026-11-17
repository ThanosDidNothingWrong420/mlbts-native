**MLB The Show 26 for PC 0.1.5: frame rates above 60, and the game's own code runs about a quarter faster.**

> [!IMPORTANT]
> **No game files are included.** You need your own copy of MLB The Show 26 with the **1.0.20** update (the 1.0.15
> update still works), dumped as NSP files from **your own** Nintendo Switch, and your Switch's `prod.keys`. This
> download contains no game code, data or keys, and no tools to obtain them. The setup builds the game on your PC,
> from your files, for your own use: don't share the installed folder. This is a fan project, not affiliated with or
> endorsed by Sony Interactive Entertainment, San Diego Studio, MLB or Nintendo.

## What's new

- **Frame rate limit.** Press **F1 → Display → Frame rate limit** and pick 30, 40, 45, 60 (the default), 90, 120,
  144, 165, 240 or your monitor's refresh rate. It applies at once. The game keeps its normal speed at any limit: MLB
  already moves its game by the real time each frame takes, so the port only lets it skip its wait for the next 60 Hz
  tick. No game code is changed.
  - Loading screens stay at 60 fps.
  - A limit above your monitor's refresh rate shows only with **Presentation** set to Fast VSync or Off.
  - Held buttons and stick directions in menus repeat at the same speed at every frame rate (the game counts frames
    for that, so at 240 fps they repeated four times faster until the port corrected it).
- **The game's own code runs much faster.** Most of each frame in a match is the game's own code, translated from
  the Switch's processor. The translation now keeps the game's working values in the PC processor's registers far
  more often instead of in memory, handles math on "not a number" values in one step, and is compiled with more
  optimisation. In an exhibition match with the limit at 240 (Ryzen 5 5600X) the frame rate went from about 110 to
  about 139 fps. At 60 fps this means more headroom, so fewer drops on slower processors.
- **Less work for the port on the game's main thread**: controller reads, calls into the graphics code and settings
  checks each cost less per frame.

## Updating

Start `MLBTS-Setup.exe` (or wait for its launcher screen) and press **Download and update**. It rebuilds the game in a
few minutes and keeps your game files, saves and settings.

From 0.1.0: download `MLBTS-Setup-0.1.5-windows.zip`, unpack it, start **`MLBTS-Setup.exe`** and press **Update**.
You can delete the old setup folder afterwards.

## Download

| File | For |
|---|---|
| `MLBTS-Setup-0.1.5-windows.zip` | Windows 10 or 11, 64-bit |

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

- **Busy scenes can still drop below 60 fps on slower processors**, batting views especially, though less than in
  0.1.4.
- Some game screens hold their own 60 fps whatever the limit (the F1 menu row then says "the game holds 60 here").
- **The first matches stutter** while the game prepares its shaders; they are kept, so it gets smoother as you play.
- Some images and lighting are still wrong or missing in places.
- The setup takes NSP files only (no XCI or NSZ yet), and only game versions 1.0.20 and 1.0.15.

## Reporting a problem

Open an issue and attach `logs\mlb.log` from the install folder (it is replaced each time the game starts, so copy it
right after the problem). For a setup problem, attach the newest `logs\setup-*.log` from the setup folder.
