// mlbts-native: the size MLB renders its 3D scene at (MLB_RENDER_SCALE, the settings menu's Graphics tab).
//
// MLB draws its 3D scene into one image smaller than its 1920x1080 output and stretches it there in its final pass
// (vk_exec_draw.c aa_composite: fragment shader daf10a59), with the HUD drawn into a separate 1080p layer. The scene's
// size comes from a table in main's read-only data [C] (1.0.15, traced 2026-11-13 with a size trace on
// nvnTextureBuilderSetSize2D): f_70005a5368 returns entry min(level, 4) of five {w, h} pairs whose address it loads
// from 0x7002283d80 (the table sits at 0x7001d818a0):
//     960x540  1280x720  1366x768  1440x810  1600x900
// and f_70005a4634 resizes the scene's render targets (f_70005a4b88: colour, depth and a third target, all 1440x810
// on the TV) to it. Docked, the game picks level 3, 1440x810: 75 % of the output on each side. Its output size comes
// from a second table at 0x7001d81890 (1280x720 handheld, 1920x1080 docked), left alone here.
//
// MLB_RENDER_SCALE = "game" (default: the table as shipped) or a percentage of the 1920x1080 output. Every entry is
// scaled by that percentage over the game's 75 %, so the level the game picks keeps its place (100: 1920x1080, 150:
// 2880x1620, 200: 3840x2160). Above 100 the game supersamples its scene and its final pass shrinks it to 1080p,
// which smooths edges at the cost of GPU time and video memory. The table is rewritten once, after main is loaded and
// before any guest code runs; when its bytes are not the shipped ones (another game version) it is left alone.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"

#define SCENE_TABLE_PTR 0x7002283d80ull   // main: the address f_70005a5368 loads the table's address from
#define SCENE_LEVELS 5
#define GAME_PCT 75.0                     // the docked level's share of the 1920x1080 output, per side
#define SCENE_MAX 4096                    // per side

static const uint32_t shipped[SCENE_LEVELS][2] = {{960, 540}, {1280, 720}, {1366, 768}, {1440, 810}, {1600, 900}};

void res_apply(void) {
    const char *v = cfg_value("RENDER_SCALE");
    if (!strcmp(v, "game")) return;
    char *end;
    double pct = strtod(v, &end);
    if (end == v || *end || pct < 25 || pct > 300) {
        rt_log("res: MLB_RENDER_SCALE=%s is not \"game\" or a percentage from 25 to 300; the game's own sizes stay", v);
        return;
    }
    uint32_t *t = (uint32_t *)(uintptr_t)*(const uint64_t *)(uintptr_t)SCENE_TABLE_PTR;
    if (memcmp(t, shipped, sizeof shipped)) {
        rt_log("res: the scene size table at %p is not 1.0.15's; MLB_RENDER_SCALE is ignored", (void *)t);
        return;
    }
    uint64_t page = (uint64_t)(uintptr_t)t & ~0xfffull, n = ((uint64_t)(uintptr_t)(t + 2 * SCENE_LEVELS) - page + 0xfff) & ~0xfffull;
    if (!plat_mem_protect(page, n, PLAT_PROT_RW)) {
        rt_log("res: cannot unprotect the scene size table; the game's own sizes stay");
        return;
    }
    double k = pct / GAME_PCT;
    char line[256];
    int at = 0;
    for (int i = 0; i < SCENE_LEVELS; i++) {
        // even sizes, as the game's own; at most SCENE_MAX a side
        uint32_t w = (uint32_t)lround(shipped[i][0] * k / 2) * 2, h = (uint32_t)lround(shipped[i][1] * k / 2) * 2;
        if (w > SCENE_MAX) w = SCENE_MAX;
        if (h > SCENE_MAX) h = SCENE_MAX;
        t[2 * i] = w, t[2 * i + 1] = h;
        at += snprintf(line + at, sizeof line - at, " %ux%u", w, h);
    }
    plat_mem_protect(page, n, PLAT_PROT_READ);
    rt_log("res: MLB_RENDER_SCALE=%s: the 3D scene's sizes are now%s (the TV uses the fourth, 1440x810 as shipped)", v,
           line);
}
