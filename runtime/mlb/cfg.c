// Settings for the runtime: the environment, then values set at run time (cfg_set), then the table's default.
// Interface and defaults from TotK Recomp runtime/cfg.h and cfg.c @ ae30a61 (MIT, LICENSE.totk-recomp); the settings
// file (totk.ini) and the menu's save are not ported. Keys are TotK's names: "TOTK_X" and "X" mean the same setting;
// mlbts-native: MLB_X in the environment wins over TOTK_X, and the table has MLB's own rows (LANGUAGE, TIMEZONE).
#include "cfg.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "host.h"
#include "platform/platform.h"

#define LIVE 1
#define RESTART 0

static const CfgMeta metas[] = {
    // mlbts-native rows
    {"LANGUAGE", "en-US", NULL, RESTART},   // am.c GetDesiredLanguage; the NACP lists American English only
    {"TIMEZONE", "local", "local|utc", RESTART},   // time.c: the host time zone, or UTC
    {"SPIN", "1", "0|1", RESTART},   // kernel.c spin-wait gates (TotK reads it without a row)
    {"NICKNAME", "Player", NULL, RESTART},   // account.c GetNickname (up to 32 bytes of UTF-8)
    {"PAD_MAP", "label", "label|position", LIVE},   // hid.c: XInput face buttons by label or by place
    {"RUMBLE", "1", "0|1", LIVE},   // hid.c: vibration on the XInput pad
    {"GPU", "vulkan", "vulkan|null", RESTART},   // main.c: the graphics back end (gfx_null.c chooses by it)
    // TotK Recomp's rows (cfg.c @ ae30a61); the TotK-only ones are read by nothing here
    // Screen
    {"FULLSCREEN", "0", "0|1", LIVE}, {"WINDOW_SIZE", "1920x1080", NULL, LIVE},
    {"VK_PRESENT", "mailbox", "mailbox|fifo|immediate", LIVE}, {"GPU_INDEX", "", NULL, RESTART},
    {"PERF_OVERLAY", "off", "off|fps|full", LIVE},
    {"OUTPUT_RES", "1920x1080", NULL, RESTART}, {"RENDER_SCALE", "game", "game|50|67|75|100|125|150|200", RESTART},
    {"UPSCALER", "fsr", "fsr|bilinear", LIVE}, {"SHARPNESS", "80", "0..100", LIVE},
    {"DLAA", "0", "0|1", RESTART}, {"DLSS", "dlaa", "dlaa|quality|balanced|performance|ultra_performance", LIVE},
    {"DLSS_PRESET", "default", "default|k|j", LIVE},
    {"DLSS_CHAR_MASK", "65", "0|35|65|100", LIVE}, {"FXAA", "1", "0|1", LIVE},
    {"ASPECT", "fit", "fit|stretch", LIVE}, {"MOVIE_ASPECT", "keep", "keep|stretch", LIVE}, {"FOV", "100", "70..140", LIVE},
    {"ANISO", "game", "game|2|4|8|16", RESTART},   // vk_texture.c sampler_features
    // colour adjustment (vk_color.c); the preset is the menu's, the four values are what the present uses
    {"COLOR_PRESET", "default", "default|vivid|clarity|custom", LIVE}, {"COLOR_BRIGHTNESS", "0", "-30..30", LIVE},
    {"COLOR_CONTRAST", "100", "70..150", LIVE}, {"COLOR_SATURATION", "100", "50..200", LIVE},
    {"COLOR_GAMMA", "1", "0.8..1.2", LIVE},
    // HDR output (vk_hdr.c): on at start (the tonemap shader is swapped as shaders load); the nits apply at once
    {"HDR", "0", "0|1", RESTART}, {"HDR_PEAK_NITS", "1000", "400..10000", LIVE}, {"HDR_GAME_NITS", "203", "80..500", LIVE},
    {"HDR_UI_NITS", "203", "80..500", LIVE}, {"HDR_HUE", "80", "0..100", LIVE},
    {"DEVICE_PROFILE", "auto", "auto|desktop|deck", RESTART},
    {"SHADOW_RES", "game", "game|256|512|768|1024|1536|2048", RESTART},
    {"SHADOW_SCALE", "1", "1..4", RESTART},
    {"SHADOW_RANGE", "100", "50..400", RESTART}, {"CONTACT_SHADOWS", "0", "0..100", RESTART},
    {"PCSS", "0", "0..200", RESTART},
    // Game
    {"FPS_LIMIT", "60", "30|40|45|60|90|120|144|display", LIVE}, {"FPS60", "1", "0|1", LIVE}, {"FRAME_QUEUE", "0", "0|1", LIVE},
    {"REALTIME", "1", "0|1", RESTART}, {"CLOTH_REALTIME", "1", "0|1", RESTART},
    {"MODS_REBUILD", "auto", "auto|off", RESTART},
    {"HIGHFPS_FIX", "1", "0|1", RESTART},
    {"LANG", "", "ja|en-US|fr|de|it|es|zh-CN|ko|nl|pt|ru|zh-TW|en-GB|fr-CA|es-419|zh-Hans|zh-Hant|pt-BR", RESTART},
    {"OPERATION_MODE", "docked", "docked|handheld", RESTART},
    {"ASSUME_FOCUS", "1", "0|1", LIVE},   // read on every focus change (ov_system.c)
    // Sound, Controls, Menu
    {"VOLUME", "100", "0..100", LIVE}, {"AUDIO_CHANNELS", "stereo", "auto|stereo|5.1", RESTART},
    {"MOTION", "1", "0|1", LIVE}, {"RUMBLE_STRENGTH", "130", "0..300", LIVE}, {"RUMBLE_CURVE", "0.6", "0.2..1.5", LIVE},
    {"RUMBLE_TRIGGERS", "0", "0|1", LIVE}, {"PAD_LAYOUT", "auto", "auto|nintendo|xbox", LIVE},
    {"PROMPTS", "nintendo", "nintendo|xbox|pc", RESTART},
    {"STICK_DEADZONE", "8", "0..50", LIVE}, {"STICK_SENS", "1", "0.1..3", RESTART},
    {"LSTICK_DEADZONE", "", "0..50", LIVE}, {"RSTICK_DEADZONE", "", "0..50", LIVE},
    {"MOUSE_MODE", "direct", "stick|direct", LIVE},
    {"MOUSE_SENS", "1", "0..10", LIVE}, {"MOUSE_AIM_SCALE", "1", "0.1..3", LIVE}, {"MENU_CURSOR", "1", "0|1", LIVE},
    {"WALK_KEY", "none", NULL, LIVE}, {"WALK_SCALE", "0.51", "0.2..0.9", LIVE},
    {"MENU_LANG", "", "en|ru|pt-BR|zh-Hans", LIVE},
    {"AMIIBO_DIR", "", NULL, LIVE},
    // Discord Rich Presence (discord_rpc.c): off by default, the Menu tab of the settings menu switches it.
    {"DISCORD_RPC", "0", "0|1", LIVE},
    {"DISCORD_APP_ID", "1567232599846293504", NULL, LIVE},
    {"DISCORD_IMAGE", "totk_512x512", NULL, LIVE},
    // Advanced: scheduling
    {"ASYNC_PREEMPT", "1", "0|1|2", RESTART}, {"REPLAY_ASYNC", "0", "0|1", RESTART},
    {"FREE_CORES", "0", "0|1", RESTART}, {"TIMER_1MS", "1", "0|1", RESTART},
    // shaders and pipelines
    {"PIPE_WAIT", "default", "default|draw", RESTART}, {"PIPE_SKIP", "fullscreen", "strict|any|fullscreen", RESTART},
    {"LIB_PREBUILD", "1", "0|1", RESTART}, {"LIB_PREBUILD_THREADS", "", "1..64", RESTART},
    {"PIPE_PREBUILD", "1", "0|1", RESTART}, {"PREWARM_XLATE", "", "1..64", RESTART}, {"GPL", "1", "0|1", RESTART},
    // textures and memory
    {"ASTC", "auto", "auto|gpu|native|cpu", RESTART}, {"MEMORY", "auto", "auto|import|mirror|verify", RESTART},
    {"TEX_ALIAS", "1", "0|1", RESTART}, {"TEX_VRAM_PRESSURE", "90", "0..100", RESTART}, {"ASTC_BC", "", "0|1", RESTART}, {"ZSTD_NATIVE", "1", "0|1", RESTART}, {"ZSTD_CHECK", "0", "0|1", RESTART},
    // render fixes
    {"LOD_GUARD", "1", "0|1", RESTART}, {"FIX_C0", "1", "0|1", RESTART}, {"FIX_ZERO", "1", "0|1", RESTART},
    {"BIND_SKIP", "1", "0|1", RESTART}, {"SHADER_MEMO", "1", "0|1", RESTART}, {"PIPE_MEMO", "1", "0|1", RESTART}, {"TEX_MEMO", "1", "0|1", RESTART},
    {"VB_USED", "1", "0|1", RESTART},
    {"DESC_TEMPLATE", "1", "0|1", RESTART},
    {"PUSH_SKIP", "1", "0|1", RESTART}, {"SPLIT_DRAWS", "64", "0..1000", RESTART},
    {"SPLIT_US", "1000", "0..20000", RESTART},
    // Vulkan
    {"VK_VALIDATION", "0", "0|1", RESTART}, {"VK_DYN_VERTEX", "", "0|1", RESTART},
    {"VK_DYN_A2C", "", "0|1", RESTART}, {"VK_SUBGROUP_PIN", "1", "0|1", RESTART},
    // diagnostics
    {"TRACE", "", NULL, RESTART}, {"FLIGHT", "0", "0|1", RESTART}, {"FLIGHT_SPIKE_MS", "100", "0..60000", RESTART},
    {"WB_FENCE_LIMIT", "65536", "0..1073741824", RESTART}, {"WB_MERGE", "1", "0|1", RESTART}, {"WB_EXACT", "1", "0|1", RESTART},
    {"PASS_CRUMBS", "", "0|1", RESTART},   // vk_crumbs.c pass marks; unset: VK_AMD_buffer_marker only
    {"INPUT_RECORD", "", NULL, RESTART},
};

// Values set at run time (cfg_set): a small table under a lock; strings are never freed, as readers may hold them.
#define MAX_SET 64
static struct { char key[64]; const char *value; } set_values[MAX_SET];
static int n_set;
static PlatMutex set_lock = PLAT_MUTEX_INIT;
static unsigned version = 1;

void cfg_init(void) {}

// "TOTK_X", "MLB_X" or "X" -> "X", upper case, into k (64 bytes).
static const char *norm_key(const char *name, char *k) {
    if (!strncmp(name, "TOTK_", 5)) name += 5;
    else if (!strncmp(name, "MLB_", 4)) name += 4;
    size_t i = 0;
    for (; name[i] && i < 63; i++) k[i] = (char)toupper((unsigned char)name[i]);
    k[i] = 0;
    return k;
}

const char *cfg_get(const char *name) {
    char k[64], var[80];
    norm_key(name, k);
    snprintf(var, sizeof var, "MLB_%s", k);
    const char *v = getenv(var);
    if (v) return v;
    snprintf(var, sizeof var, "TOTK_%s", k);
    if ((v = getenv(var))) return v;
    plat_mutex_lock_shared(&set_lock);
    for (int i = 0; i < n_set && !v; i++)
        if (!strcmp(set_values[i].key, k)) v = set_values[i].value;
    plat_mutex_unlock_shared(&set_lock);
    return v;
}

void cfg_set(const char *name, const char *value) {
    char k[64];
    norm_key(name, k);
    const char *copy = value ? _strdup(value) : NULL;   // MSVC CRT name
    plat_mutex_lock(&set_lock);
    int i = 0;
    while (i < n_set && strcmp(set_values[i].key, k)) i++;
    if (i == n_set) {
        if (n_set == MAX_SET) rt_fatal("cfg: more than %d settings set at run time", MAX_SET);
        snprintf(set_values[n_set++].key, sizeof set_values[0].key, "%s", k);
    }
    set_values[i].value = copy;
    version++;
    plat_mutex_unlock(&set_lock);
}

// mlbts-native: settings from mlb.cfg beside the executable, one KEY=VALUE per line (MLB_X, TOTK_X or X; '#' or ';'
// starts a comment), so a player can switch an experiment without a new build or environment variables. Applied with
// cfg_set before anything reads a setting: the environment still wins, and the defaults main.c sets for unset keys
// leave a file's value alone. Returns how many settings it applied (0 without a file).
int cfg_load_file(void) {
    wchar_t dir[1024];
    if (!plat_exe_dir_wide(dir, 1000)) return 0;
    wcscat(dir, L"mlb.cfg");
    FILE *f = _wfopen(dir, L"r");
    if (!f) return 0;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        char *hash = strpbrk(p, "#;");
        if (hash) *hash = 0;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = p, *v = eq + 1, *e;
        for (e = k + strlen(k); e > k && (e[-1] == ' ' || e[-1] == '\t'); e--) *(e - 1) = 0;
        while (*v == ' ' || *v == '\t') v++;
        for (e = v + strlen(v); e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'); e--) *(e - 1) = 0;
        if (!*k) continue;
        cfg_set(k, v);
        rt_log("cfg: mlb.cfg: %s=%s%s", k, v, getenv(k) ? " (the environment overrides it)" : "");
        n++;
    }
    fclose(f);
    if (!n) rt_log("cfg: mlb.cfg read, no settings in it");
    return n;
}

unsigned cfg_version(void) { return version; }
int cfg_is_set(const char *name) { return cfg_get(name) != NULL; }
int cfg_save(void) { return 0; }   // no settings file: values set at run time last for the run

const CfgMeta *cfg_meta(const char *key) {
    char k[64];
    norm_key(key, k);
    for (size_t i = 0; i < sizeof metas / sizeof *metas; i++)
        if (!strcmp(metas[i].key, k)) return &metas[i];
    return NULL;
}

const char *cfg_value(const char *name) {
    const char *e = cfg_get(name);
    if (e && *e) return e;
    const CfgMeta *m = cfg_meta(name);
    if (!m) rt_fatal("cfg: %s has no row in the settings table", name);
    return m->def;
}

static double default_number(const char *name) { return strtod(cfg_meta(name)->def, NULL); }

double cfg_double(const char *name) {
    const char *v = cfg_value(name);
    char *end;
    double d = strtod(v, &end);
    return end == v || *end || d != d ? default_number(name) : d;
}

int cfg_int(const char *name) {
    const char *v = cfg_value(name);
    char *end;
    long l = strtol(v, &end, 10);
    return end == v || *end ? (int)default_number(name) : (int)l;
}

int cfg_bool(const char *name) { return strcmp(cfg_value(name), "0") != 0; }
