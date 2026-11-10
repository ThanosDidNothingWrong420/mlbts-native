@echo off
rem Runs work\build\mlb.exe under RenderDoc. F12 (or Print Screen) in the game saves a capture as
rem work\scratch\rd\mlb_frame<N>.rdc; the port's own F12 capture moves to F9 while RenderDoc is loaded.
rem renderdoccmd.exe is looked for in %RENDERDOC_DIR%, Program Files, then %USERPROFILE%\tools\renderdoc-*.
setlocal
set "ROOT=%~dp0.."
set "RD="
if defined RENDERDOC_DIR if exist "%RENDERDOC_DIR%\renderdoccmd.exe" set "RD=%RENDERDOC_DIR%\renderdoccmd.exe"
if not defined RD if exist "%ProgramFiles%\RenderDoc\renderdoccmd.exe" set "RD=%ProgramFiles%\RenderDoc\renderdoccmd.exe"
if not defined RD for /r "%USERPROFILE%\tools" %%f in (renderdoccmd.exe) do if not defined RD if exist "%%f" set "RD=%%f"
if not defined RD (
    echo renderdoccmd.exe not found: set RENDERDOC_DIR to the RenderDoc folder
    exit /b 1
)
if not exist "%ROOT%\work\scratch\rd" mkdir "%ROOT%\work\scratch\rd"
cd /d "%ROOT%\work\build"
rem --opt-soft-memory-limit (MiB): RenderDoc prepares a capture's resource copies in batches under it. Without it a
rem capture in a match copied everything at once and ran out of memory (renderdoc.dll crashed at the F12 press).
"%RD%" capture -w --opt-soft-memory-limit 2048 -c "%ROOT%\work\scratch\rd\mlb" "%ROOT%\work\build\mlb.exe" %*
