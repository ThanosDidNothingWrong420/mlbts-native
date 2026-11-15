/* MLBTS-Setup.exe: starts python\pythonw.exe -m installer.gui with this exe's folder as the working directory, without
   a console window. tools/make_release.py builds it with the release's llvm-mingw:
       llvm/bin/llvm-windres -I runtime/mlb -O coff launcher.rc -o launcher_rc.o
       llvm/bin/clang -O2 -mwindows launcher.c launcher_rc.o -o MLBTS-Setup.exe
   After TotK Recomp installer/launcher/launcher.c @ v0.4.0 (MIT), English only.
   System DPI aware, like the window's Python process (installer/gui.py): the error box is drawn at the display's
   scale instead of stretched. */
#include <windows.h>
#include <wchar.h>

#define PATH_CHARS (MAX_PATH * 2)

static void fail(const wchar_t *what)
{
    wchar_t text[PATH_CHARS + 256];
    swprintf(text, sizeof text / sizeof *text,
             L"The setup could not start: %ls.\n\nUnpack the whole download into an empty folder (not inside the zip "
             L"viewer) and start MLBTS-Setup.exe from there.", what);
    MessageBoxW(NULL, text, L"MLB The Show 26 - PC port setup", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    wchar_t folder[PATH_CHARS], python[PATH_CHARS], command[PATH_CHARS + 64];
    DWORD n = GetModuleFileNameW(NULL, folder, PATH_CHARS);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    (void)instance; (void)previous; (void)command_line; (void)show;
    SetProcessDPIAware();   /* before the message box exists */
    if (n == 0 || n >= PATH_CHARS) { fail(L"its own path is too long"); return 1; }
    while (n > 0 && folder[n - 1] != L'\\') n--;
    folder[n > 0 ? n - 1 : 0] = 0;
    if (swprintf(python, PATH_CHARS, L"%ls\\python\\pythonw.exe", folder) < 0 ||
        GetFileAttributesW(python) == INVALID_FILE_ATTRIBUTES) { fail(L"python\\pythonw.exe is missing"); return 1; }
    if (swprintf(command, PATH_CHARS + 64, L"\"%ls\" -I -m installer.gui", python) < 0) { fail(L"path too long"); return 1; }
    if (!CreateProcessW(python, command, NULL, NULL, FALSE, 0, NULL, folder, &si, &pi)) {
        fail(L"python\\pythonw.exe did not start");
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
