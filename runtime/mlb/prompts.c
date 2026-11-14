// mlbts-native: the game's button prompts show the controller in use: Xbox or PlayStation icons in place of the
// Switch ones.
//
// MLB writes prompts as text tokens (^b0^ a button, ^b16^ a stick) and draws each as a cell of one 1024x1024 BC7
// texture, the "symbols" entry of NSW/UI/ART/FONTS/FONTS.TEX (a RenderDoc capture of the main menu: the "More Options"
// prompt samples it). That texture holds only Switch buttons; the RomFS has no Xbox or PlayStation art (its PS4/ and
// PS5/ folders are animation, stadium and subtitle files). tools/make_prompts.py draws the console icons in the game's
// style and compresses them to BC7 blocks (work/gen/prompt_icons.h, made by tools/build.py); vk_texture.c copies the
// blocks of the chosen set over its image after every upload, and uploads it again from its storage when the set
// changes (the storage keeps the Switch icons, so going back to them is an upload).
//
// Which set: MLB_PROMPTS (the settings menu's Controls tab) = auto (default: the kind of the controller used last,
// hid.c; the game's own icons with no controller or a Nintendo one), nintendo, xbox or playstation. The face buttons
// follow MLB_PAD_MAP: by label Switch A shows Xbox A or cross; by position it shows the button in the A button's place
// (Xbox B, circle). Both take effect at once.
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include "prompt_icons.h"
#include "prompts.h"

enum { ICONS_FORMAT = 0x4e, ICONS_SIZE = 1024 };   // NVN RGBA_BPTC_UNORM_SRGB (BC7 sRGB)
// FNV-1a over the first 16 KiB of the icon texture's storage (1.0.15). Those bytes are its top rows, the Switch
// sticks and d-pad icons, which no other texture of that shape shares.
#define ICONS_HASH 0x654fe56d4ffb4ee7ull
enum { HASH_BYTES = 16384 };

int mlb_prompts_shape(int nvn_format, uint32_t width, uint32_t height, uint32_t levels) {
    return nvn_format == ICONS_FORMAT && width == ICONS_SIZE && height == ICONS_SIZE && levels == 1;
}

int mlb_prompts_is_icons(uint64_t addr, const void *storage, size_t size) {
    if (!storage || size < HASH_BYTES) return 0;
    const uint8_t *p = storage;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < HASH_BYTES; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    if (h == ICONS_HASH) return 1;
    static volatile int32_t n_logged;
    if (__atomic_fetch_add(&n_logged, 1, __ATOMIC_RELAXED) < 16)
        rt_log("prompts: texture %#llx (1024x1024 BC7): hash %016llx, not the button icons",
               (unsigned long long)addr, (unsigned long long)h);
    return 0;
}

int mlb_prompts_set(void) {
    // the setting's strings are looked at again only when a setting changed (the menu, cfg_set)
    static unsigned seen = ~0u;
    static int choice, position;   // choice: 0 auto, 1 nintendo, 2 xbox, 3 playstation
    unsigned v = cfg_version();
    if (v != seen) {
        const char *p = cfg_value("MLB_PROMPTS");
        choice = !strcmp(p, "nintendo") ? 1 : !strcmp(p, "xbox") ? 2 : !strcmp(p, "playstation") ? 3 : 0;
        position = !strcmp(cfg_value("MLB_PAD_MAP"), "position");
        seen = v;
    }
    int kind = choice == 0 ? hid_prompt_kind() : choice - 1;   // 1 Xbox, 2 PlayStation    if (kind != 1 && kind != 2) return -1;
    return (kind - 1) * 2 + position;   // prompt_icons.h: xbox by label, by position, playstation by label, by position
}

static MlbPromptRun *set_runs[PROMPT_SETS];
static int set_nruns[PROMPT_SETS], set_nblocks[PROMPT_SETS];
static PlatOnce runs_once = PLAT_ONCE_INIT;

static void runs_build(void *arg) {
    (void)arg;
    for (int s = 0; s < PROMPT_SETS; s++) {
        int n = 0;
        for (int i = 0; i < PROMPT_SET_PATCHES; i++) n += prompt_patches[prompt_sets[s][i]][1];
        MlbPromptRun *r = calloc((size_t)n, sizeof *r);
        if (!r) rt_fatal("prompts: out of memory");
        int k = 0, blocks = 0;
        for (int i = 0; i < PROMPT_SET_PATCHES; i++) {
            const uint16_t *pt = prompt_patches[prompt_sets[s][i]];
            for (int j = pt[0]; j < pt[0] + pt[1]; j++) {
                const uint16_t *run = prompt_runs[j];
                r[k++] = (MlbPromptRun){run[0], run[1], run[2], prompt_blocks + (size_t)run[3] * 16};
                blocks += run[2];
            }
        }
        set_runs[s] = r, set_nruns[s] = n, set_nblocks[s] = blocks;
    }
}

int mlb_prompts_runs(int set, const MlbPromptRun **runs, int *blocks) {
    if (set < 0 || set >= PROMPT_SETS) return 0;
    plat_once(&runs_once, runs_build, NULL);
    *runs = set_runs[set];
    *blocks = set_nblocks[set];
    return set_nruns[set];
}
