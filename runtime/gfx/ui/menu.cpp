// The in-game settings menu (menu.h): Dear ImGui state, the event queue the window adapter fills, gamepad navigation,
// the Vulkan back end on our device and the menu's pages. Free of any window system. ImGui runs on the present path
// only, under gfx_present.c's present lock; the event functions only queue.
//
// From TotK Recomp runtime/ui/menu.cpp @ ae30a61 (MIT, Copyright (c) 2026 Valiantsin Dzerakh): the open state and event
// queue, gamepad navigation, the Vulkan set-up (after LAS Recomp's runtime/nvn/vulkan/imgui_glue.cpp,
// https://github.com/peiikai/LASRecomp @ da35b83a9611d046a9a681aae668d7085954d37f, MIT, Copyright (c) 2026 Peiikai)
// and the look (after SavageWindRecomp's runtime/ui/menu.mm, https://github.com/Revivedx/SavageWindRecomp @
// 575ad88e02f9a89a9a885d90039d48af33c982f4, MIT, Copyright (c) 2026 Revivedx and the Savage Wind Recomp contributors,
// Copyright (c) 2026 Peiikai): dark green panels, gold accents, the tab bar with L / R or Q / E, the window over a
// dimmed game, two-pane option rows (option_rows.cpp).
//
// mlbts-native: MLB's pages (Graphics, Display, Controls) over the settings this port reads live; English text only
// (TotK's translated catalog, its mods, cheats, amiibo, binds and performance overlay pages are not ported). F1, or
// Minus and Plus together on the pad (Back and Start, hid.c), open and close it; B or Esc close it. While it is open the
// game keeps running and gets an idle controller (hid.c). Changes apply at once and go into mlb.cfg (cfg_save).
// Closed, it costs one atomic load per present on top of the F12 capture check (gfx_stubs.c mlb_capture_tick).
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "imgui_internal.h"   // ClosePopupToLevel
#include "backends/imgui_impl_vulkan.h"
#include "ui/option_rows.h"

extern "C" {
#include "cfg.h"
#include "platform/platform.h"
#include "ui/menu.h"
// host.h and vk.h pull in cpu.h, which is C only: the functions the menu needs from them, as declared there.
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vk_queue_lock(void);
void vk_queue_unlock(void);
extern const char rt_port_version[];
int hid_menu_pad(uint64_t *buttons, int32_t *lx, int32_t *ly);   // mlb/hid.c
int vk_aa_status(void);                                          // vk_aa.c
int mlb_capture_tick(uint64_t frame, const char **shot);         // gfx_stubs.c: F12 / MLB_SHOT captures
}

// Roboto Medium (Apache-2.0, ui/fonts/LICENSE-Roboto.txt) with Latin glyphs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"   // #embed in C++
static const unsigned char roboto_ttf[] = {
#embed "fonts/Roboto-Medium.ttf"
};
#pragma clang diagnostic pop

// ---- open state, any thread
static std::atomic<int> g_open;     // the menu is open
static std::atomic<int> g_reset;    // opened or closed since the last laid-out frame: drop stale input
static std::atomic<int> g_failed;   // set-up failed for good (the menu stays closed)

// g_open and the log line change together under open_lock, so they cannot disagree when the window thread (F1), the
// pad's polling thread (Minus+Plus) and the present path (Continue, B) race.
static PlatMutex open_lock = PLAT_MUTEX_INIT;

// want 0 or 1 sets the state; -1 flips it. Returns the new state.
static int change_open(int want) {
    plat_mutex_lock(&open_lock);
    int open = want < 0 ? !g_open.load() : want;
    if (g_open.load() != open) {
        g_open.store(open);
        g_reset.store(1);
        rt_log("menu: %s", open ? "open" : "closed");
    }
    plat_mutex_unlock(&open_lock);
    return open;
}

extern "C" int menu_is_open(void) { return g_open.load(std::memory_order_relaxed); }
extern "C" void menu_toggle(void) {
    if (!g_failed.load(std::memory_order_relaxed)) change_open(-1);
}
extern "C" void menu_perf_cycle(void) {}   // TotK's performance overlay: not ported

// ---- events from the window adapter, queued while the menu is open; the present path hands them to ImGui
enum { EV_KEY, EV_POS, EV_BUTTON, EV_WHEEL, EV_FOCUS, EV_MAX = 256 };
struct Ev { int type, a, b; float x, y; };
static PlatMutex ev_lock = PLAT_MUTEX_INIT;
static Ev ev_q[EV_MAX];
static int ev_n;

static void push(Ev e) {
    if (!menu_is_open()) return;
    plat_mutex_lock(&ev_lock);
    if (e.type == EV_POS && ev_n && ev_q[ev_n - 1].type == EV_POS) ev_q[ev_n - 1] = e;   // only the newest position
    else if (ev_n < EV_MAX) ev_q[ev_n++] = e;
    plat_mutex_unlock(&ev_lock);
}

extern "C" void menu_event_key(int key, int down) { push({EV_KEY, key, down, 0, 0}); }
extern "C" void menu_event_mouse_pos(float x, float y) { push({EV_POS, 0, 0, x, y}); }
extern "C" void menu_event_mouse_button(int button, int down) { push({EV_BUTTON, button, down, 0, 0}); }
extern "C" void menu_event_wheel(float dx, float dy) { push({EV_WHEEL, 0, 0, dx, dy}); }
extern "C" void menu_event_focus(int focused) { push({EV_FOCUS, focused, 0, 0, 0}); }

static void drain_events(ImGuiIO &io) {
    Ev q[EV_MAX];
    plat_mutex_lock(&ev_lock);
    int n = ev_n;
    for (int i = 0; i < n; i++) q[i] = ev_q[i];
    ev_n = 0;
    plat_mutex_unlock(&ev_lock);
    for (int i = 0; i < n; i++) {
        const Ev &e = q[i];
        switch (e.type) {
        case EV_KEY: io.AddKeyEvent((ImGuiKey)e.a, e.b != 0); break;
        case EV_POS: io.AddMousePosEvent(e.x, e.y); break;
        case EV_BUTTON: io.AddMouseButtonEvent(e.a, e.b != 0); break;
        case EV_WHEEL: io.AddMouseWheelEvent(e.x, e.y); break;
        case EV_FOCUS: io.AddFocusEvent(e.a != 0); break;
        }
    }
}

// ---- gamepad navigation: the Switch buttons of the XInput pad (hid.c, after MLB_PAD_MAP) to ImGui's gamepad keys.
// A confirms, B goes back, L / R switch tabs.
enum { P_A = 0, P_B = 1, P_X = 2, P_Y = 3, P_L = 6, P_R = 7, P_ZL = 8, P_ZR = 9, P_LEFT = 12, P_UP = 13, P_RIGHT = 14,
       P_DOWN = 15 };
static const struct { int bit; ImGuiKey key; } pad_keys[] = {
    {P_A, ImGuiKey_GamepadFaceDown}, {P_B, ImGuiKey_GamepadFaceRight}, {P_X, ImGuiKey_GamepadFaceUp},
    {P_Y, ImGuiKey_GamepadFaceLeft}, {P_L, ImGuiKey_GamepadL1}, {P_R, ImGuiKey_GamepadR1},
    {P_ZL, ImGuiKey_GamepadL2}, {P_ZR, ImGuiKey_GamepadR2}, {P_LEFT, ImGuiKey_GamepadDpadLeft},
    {P_UP, ImGuiKey_GamepadDpadUp}, {P_RIGHT, ImGuiKey_GamepadDpadRight}, {P_DOWN, ImGuiKey_GamepadDpadDown},
};
static uint64_t pad_prev;   // buttons at the last frame; a button held when the menu opened counts once released
static int stick_dir = -1;  // the D-pad button the left stick holds, -1 none

// The left stick moves the cursor as the D-pad does (ImGui's own stick handling only scrolls the page): past half way
// it holds the D-pad direction of its larger axis, kept until that axis falls under 0.35; ImGui repeats a held one.
static int stick_to_dpad(int32_t lx, int32_t ly) {
    float x = lx / 32767.0f, y = ly / 32767.0f;   // y up
    float along = stick_dir == P_LEFT ? -x : stick_dir == P_RIGHT ? x : stick_dir == P_UP ? y : stick_dir == P_DOWN ? -y : 0;
    if (stick_dir >= 0 && along > 0.35f) return stick_dir;
    if (fmaxf(fabsf(x), fabsf(y)) < 0.5f) return -1;
    return fabsf(x) > fabsf(y) ? (x < 0 ? P_LEFT : P_RIGHT) : (y > 0 ? P_UP : P_DOWN);
}

static void pad_events(ImGuiIO &io, int reset) {
    uint64_t b;
    int32_t lx, ly;
    int have = hid_menu_pad(&b, &lx, &ly);
    if (have) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    stick_dir = stick_to_dpad(lx, ly);
    if (stick_dir >= 0) b |= 1ull << stick_dir;
    if (reset) {
        pad_prev = b;
        return;
    }
    for (const auto &k : pad_keys) {
        uint64_t m = 1ull << k.bit;
        if ((b ^ pad_prev) & m) io.AddKeyEvent(k.key, (b & m) != 0);
    }
    pad_prev = b;
}

// ---- ImGui and its Vulkan back end (present path)
static int g_ctx_ok;              // context made
static int g_vk_ok;               // back end initialised for g_vk_fmt
static VkFormat g_vk_fmt;
static float g_font_scale;        // the scale of the uploaded font atlas; 0 when none is uploaded
static float g_style_scale;       // the scale the style was last sized for
static ImGuiStyle g_base_style;   // the menu's style at 1080 lines
static uint64_t g_last_ns;

// Sizes are tuned for 1080 lines and follow the window's height, at least the 720-line size scaled by the display's DPI.
static float menu_scale(uint32_t h) {
    float least = 720.0f * (float)gfx_window_dpi() / 96.0f;
    return ((float)h >= least ? (float)h : least) / 1080.0f;
}

static void style_init(ImGuiStyle &s) {
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 14; s.FrameRounding = 8; s.GrabRounding = 8; s.TabRounding = 8; s.ChildRounding = 10;
    s.WindowPadding = ImVec2(28, 22); s.FramePadding = ImVec2(14, 8); s.ItemSpacing = ImVec2(14, 12);
    s.WindowBorderSize = 0; s.ScrollbarSize = 18;
    ImVec4 *c = s.Colors;
    ImVec4 gold(0.93f, 0.76f, 0.33f, 1), green(0.15f, 0.36f, 0.25f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.09f, 0.08f, 0.94f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text] = ImVec4(0.96f, 0.94f, 0.88f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.62f, 0.64f, 0.58f, 1);
    c[ImGuiCol_Button] = ImVec4(0.14f, 0.22f, 0.18f, 1);
    c[ImGuiCol_ButtonHovered] = green;
    c[ImGuiCol_ButtonActive] = ImVec4(0.21f, 0.50f, 0.34f, 1);
    c[ImGuiCol_Tab] = ImVec4(0.12f, 0.18f, 0.15f, 1);
    c[ImGuiCol_TabHovered] = green;
    c[ImGuiCol_TabSelected] = ImVec4(0.21f, 0.44f, 0.31f, 1);
    c[ImGuiCol_TabSelectedOverline] = gold;
    c[ImGuiCol_NavCursor] = gold;
    c[ImGuiCol_Separator] = ImVec4(0.30f, 0.36f, 0.30f, 1);
}

static void make_context(void) {
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendPlatformName = "mlb_menu";
    style_init(g_base_style);
    ImGui::GetStyle() = g_base_style;
    g_ctx_ok = 1;
}

static void vk_down(void) {
    if (!g_vk_ok) return;
    ImGui_ImplVulkan_Shutdown();
    g_vk_ok = 0;
    g_font_scale = 0;
}

static int vk_up(const MenuTarget *t) {
    if (g_vk_ok && g_vk_fmt == t->format) return 1;
    if (g_vk_ok) {   // the swapchain changed format (the game switched sRGB / UNORM): pipelines for the new one
        vk_queue_lock();
        vkQueueWaitIdle(t->queue);
        vk_queue_unlock();
        vk_down();
    }
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.ApiVersion = VK_API_VERSION_1_3;
    ii.Instance = t->instance;
    ii.PhysicalDevice = t->phys;
    ii.Device = t->device;
    ii.QueueFamily = t->queue_family;
    ii.Queue = t->queue;
    ii.DescriptorPoolSize = 16;
    ii.MinImageCount = 2;
    ii.ImageCount = 4;   // vertex buffers rotate per recorded frame: more than gfx_present.c's 2 frames in flight
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.UseDynamicRendering = true;
    g_vk_fmt = t->format;
    ii.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &g_vk_fmt;
    if (!ImGui_ImplVulkan_Init(&ii)) return 0;
    g_vk_ok = 1;
    return 1;
}

// Roboto Medium (Latin) at 26 px per 1080 lines. Made again when the window height changes; the old texture goes after
// the queue is idle (CreateFontsTexture).
static int fonts_up(float scale) {
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0};
    ImFontAtlas *fa = ImGui::GetIO().Fonts;
    fa->Clear();
    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    fa->AddFontFromMemoryTTF((void *)roboto_ttf, (int)sizeof roboto_ttf, 26.0f * scale, &fc, ranges);
    vk_queue_lock();   // the upload submits on the queue and waits for it
    int ok = ImGui_ImplVulkan_CreateFontsTexture();
    vk_queue_unlock();
    fa->ClearInputData();
    fa->ClearTexData();
    rt_log("menu: font atlas at %.1f px", 26.0f * scale);
    g_font_scale = ok ? scale : 0;
    return ok;
}

extern "C" void menu_vk_release(void) {
    if (g_ctx_ok) vk_down();
}

extern "C" int menu_font_check(void) { return 1; }

// ---- the pages
enum { TAB_GRAPHICS, TAB_DISPLAY, TAB_CONTROLS, TAB_COUNT };
static const char *const kTabNames[TAB_COUNT] = {"Graphics", "Display", "Controls"};
static int g_tab;               // the tab shown
static int g_tab_request = -1;  // select this tab at the next layout; -1 none
static int g_focus_page = -1;   // this tab's first row takes the cursor once it is shown; -1 none

// What the antialiasing pass did lately (vk_aa_status), under its value.
static const char *aa_note(void) {
    switch (vk_aa_status()) {
    case 1: return "active";
    case 2: return "waiting for a 3D scene";
    case 3: return "not supported on this GPU";
    default: return nullptr;
    }
}

// Colour adjustment (vk_color.c): a profile writes the four values (TotK Recomp's presets); moving one of them makes
// the profile Custom.
static void rows_color(void) {
    static const struct { const char *name, *b, *c, *s, *g; } presets[] = {
        {"default", "0", "100", "100", "1"}, {"vivid", "0", "115", "135", "0.95"}, {"clarity", "2", "110", "115", "1"}};
    static const char *const names[] = {"Default", "Vivid", "Clarity", "Custom"};
    opt_group("Colour");
    if (opt_choice("COLOR_PRESET", "Colour profile", names, 4,
                   "Ready-made colour settings. Vivid raises contrast and saturation; Clarity is a gentler lift. "
                   "Changing any value below switches this to Custom.")) {
        const char *p = cfg_value("COLOR_PRESET");
        for (const auto &e : presets)
            if (!strcmp(p, e.name)) {
                cfg_set_saved("COLOR_BRIGHTNESS", e.b), cfg_set_saved("COLOR_CONTRAST", e.c);
                cfg_set_saved("COLOR_SATURATION", e.s), cfg_set_saved("COLOR_GAMMA", e.g);
            }
    }
    bool moved = opt_slider("COLOR_BRIGHTNESS", "Brightness", -30, 30,
                            "Lightens or darkens the whole picture, HUD included.", 1, "%+.0f %%");
    moved |= opt_slider("COLOR_CONTRAST", "Contrast", 70, 150, "Spreads bright and dark parts further apart.", 5,
                        "%.0f %%");
    moved |= opt_slider("COLOR_SATURATION", "Saturation", 50, 200, "How strong the colours are. 100 % is the game's own.",
                        5, "%.0f %%");
    moved |= opt_slider("COLOR_GAMMA", "Gamma", 0.8f, 1.2f, "Brightens (below 1) or deepens (above 1) the mid-tones.",
                        0.02f, "%.2f");
    if (moved && strcmp(cfg_value("COLOR_PRESET"), "custom")) cfg_set_saved("COLOR_PRESET", "custom");
}

static void tab_graphics(float scale) {
    opt_begin("##graphics", scale);
    opt_group("Antialiasing");
    static const char *const aa[] = {"Off", "FXAA"};
    opt_choice("AA", "Antialiasing", aa, 2,
               "Smooths the jagged, stair-stepped edges of the 3D scene (players, the field lines, the stands, the "
               "bat). The game draws its 3D scene at 1440 x 810 without any smoothing and then stretches it to "
               "1080p, which makes the steps larger.\n\n"
               "FXAA finds edges in the finished 3D picture and blends across them. It runs before the game draws "
               "its HUD and menus, so the score bug and text stay sharp. It costs a fraction of a millisecond.",
               aa_note());
    opt_slider("AA_STRENGTH", "FXAA strength", 0, 100,
               "How much FXAA also softens thin details smaller than a pixel (the netting, distant lines, "
               "shimmering on the grass). Higher is smoother but a little blurrier; 0 only smooths the edges.",
               5, "%.0f %%");
    rows_color();
    opt_end();
}

static const int kSizes[][2] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800},
                                {3840, 2160}};
enum { MAX_SIZES = sizeof kSizes / sizeof *kSizes + 1 };

// The common 16:9 sizes up to the monitor's (1920 x 1080 always), and the current one when it is another.
static void row_window_size(void) {
    int sz[MAX_SIZES][2], n = 0, mw = 0, mh = 0, cw = 0, ch = 0;
    int have = gfx_window_monitor_size(&mw, &mh);
    int cur_ok = sscanf(cfg_value("WINDOW_SIZE"), "%dx%d", &cw, &ch) == 2;
    bool listed = false;
    for (size_t i = 0; i < sizeof kSizes / sizeof *kSizes; i++) {
        int w = kSizes[i][0], h = kSizes[i][1];
        bool cur = cur_ok && w == cw && h == ch;
        if (have && (w > mw || h > mh) && !cur && !(w == 1920 && h == 1080)) continue;
        listed |= cur;
        sz[n][0] = w, sz[n][1] = h, n++;
    }
    if (cur_ok && !listed) {   // the current size where its area puts it
        int at = n;
        while (at > 0 && (long)sz[at - 1][0] * sz[at - 1][1] > (long)cw * ch) at--;
        memmove(sz[at + 1], sz[at], sizeof sz[0] * (n - at));
        sz[at][0] = cw, sz[at][1] = ch, n++;
    }
    char vals[MAX_SIZES][24], shown[MAX_SIZES][32];
    const char *pv[MAX_SIZES], *pn[MAX_SIZES];
    for (int i = 0; i < n; i++) {
        snprintf(vals[i], sizeof vals[i], "%dx%d", sz[i][0], sz[i][1]);
        snprintf(shown[i], sizeof shown[i], "%d \xc3\x97 %d", sz[i][0], sz[i][1]);   // U+00D7, the multiplication sign
        pv[i] = vals[i];
        pn[i] = shown[i];
    }
    opt_choice_list("WINDOW_SIZE", "Window size", pv, pn, n,
                    "The size of the game window when it is not fullscreen. The game itself still renders at "
                    "1080p and is scaled to the window.");
}

// "in use: VSync" under the Presentation value when the swapchain got another mode than the setting (a driver without
// mailbox gets fifo); NULL when they agree or before the first swapchain.
static const char *present_note(void) {
    const char *got = gfx_window_present_mode();
    if (!*got || !strcmp(got, cfg_value("VK_PRESENT"))) return nullptr;
    return !strcmp(got, "fifo") ? "in use: VSync" : !strcmp(got, "mailbox") ? "in use: Fast VSync" : "in use: Off";
}

static void tab_display(float scale) {
    opt_begin("##display", scale);
    opt_toggle("FULLSCREEN", "Fullscreen", "Borderless fullscreen on the monitor the window is on. F11 also switches it.");
    row_window_size();
    static const char *const present[] = {"Fast VSync", "VSync", "Off (tearing)"};
    opt_choice("VK_PRESENT", "Presentation", present, 3,
               "How finished frames reach the screen.\n\nFast VSync shows the newest frame at each refresh without "
               "tearing or waiting. VSync waits for the display's refresh (more input delay). Off shows frames as soon "
               "as they are done and can tear.",
               present_note());
    static const char *const aspect[] = {"Keep 16:9", "Stretch"};
    opt_choice("ASPECT", "Aspect ratio", aspect, 2,
               "Keep 16:9 adds black bars when the window has another shape. Stretch fills the window and distorts "
               "the picture.");
    opt_end();
}

static void tab_controls(float scale) {
    opt_begin("##controls", scale);
    static const char *const map[] = {"By label", "By position"};
    opt_choice("PAD_MAP", "Face buttons", map, 2,
               "How an Xbox-style controller's A B X Y map to the Switch buttons.\n\nBy label: Xbox A is Switch A "
               "(confirm), B is B. By position: the buttons work by where they sit, as on a Switch controller, so the "
               "east button (Xbox B) confirms.");
    opt_toggle("RUMBLE", "Vibration", "Controller rumble when the game asks for it.");
    opt_group("Keys");
    opt_text("Open or close this menu: F1, or Back and Start together on the controller. Esc or B closes it. "
             "Q / E or LB / RB switch the tabs.");
    opt_end();
}

// ---- the window: title, tabs (L / R or Q / E switch), the page, and the bottom row with "Continue" and "Quit game".
// B on the gamepad and Esc close it.
static void layout_menu(uint32_t w, uint32_t h, float scale) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(0, 0, 0, 120));
    float ww = fminf(w * 0.88f, 1400 * scale), wh = fminf(h * 0.86f, 820 * scale);
    ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_Always);
    ImGui::Begin("##menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "Settings");
    char version[96];   // right-aligned on the title line
    snprintf(version, sizeof version, "MLB The Show 26 native %s, game 1.0.15", rt_port_version);
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(version).x);
    ImGui::TextDisabled("%s", version);

    int step = ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_E, false)   ? 1
             : ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false) ? -1
                                                                                                         : 0;
    if (step) g_tab_request = (g_tab + step + TAB_COUNT) % TAB_COUNT;
    if (g_tab_request >= 0) g_focus_page = g_tab_request;   // the new page's first row takes the cursor

    const ImGuiStyle &st = ImGui::GetStyle();
    float bottom = ImGui::GetFrameHeight() + st.ItemSpacing.y * 2;
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_NoTabListScrollingButtons)) {
        for (int k = 0; k < TAB_COUNT; k++) {
            char label[64];
            snprintf(label, sizeof label, "  %s  ###tab%d", kTabNames[k], k);
            if (!ImGui::BeginTabItem(label, nullptr, g_tab_request == k ? ImGuiTabItemFlags_SetSelected : 0)) continue;
            g_tab = k;
            if (g_focus_page == k) {
                opt_focus_first();
                g_focus_page = -1;
            }
            ImGui::Spacing();
            opt_reserve_bottom(bottom);
            switch (k) {
            case TAB_GRAPHICS: tab_graphics(scale); break;
            case TAB_DISPLAY: tab_display(scale); break;
            case TAB_CONTROLS: tab_controls(scale); break;
            }
            ImGui::EndTabItem();
        }
        g_tab_request = -1;
        ImGui::EndTabBar();
    }

    // bottom row: the two buttons on the right
    ImGui::Spacing();
    float y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(y + st.FramePadding.y);
    ImGui::TextDisabled("Changes apply at once and are saved to mlb.cfg.");
    const char *cont = "Continue", *quit = "Quit game";
    float bw = fmaxf(ImGui::CalcTextSize(cont).x, ImGui::CalcTextSize(quit).x) + st.FramePadding.x * 4;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - st.WindowPadding.x - bw * 2 - st.ItemSpacing.x, y));
    if (ImGui::Button(cont, ImVec2(bw, 0))) change_open(0);
    ImGui::SameLine();
    if (ImGui::Button(quit, ImVec2(bw, 0))) gfx_window_request_close();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
        change_open(0);
    ImGui::End();
}

static int g_save_pending;   // a changed setting still waits for its write to mlb.cfg (opt_save_tick)

// mlbts-native: MLB presents sRGB images, so the swapchain is sRGB (gfx_present.c create_swapchain) and the hardware
// encodes what the menu writes. ImGui's colours are already sRGB values: written as they are, they came out encoded
// twice, the dark green panels a washed-out grey green (first test shot, 2026-11-13). On an sRGB target the vertex
// colours are turned into linear values first; alpha stays.
static bool srgb_format(VkFormat f) {
    return f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

static void linear_colours(ImDrawData *dd) {
    static uint8_t lut[256];
    if (!lut[255])
        for (int i = 0; i < 256; i++) {
            float c = i / 255.0f, l = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
            lut[i] = (uint8_t)lrintf(l * 255.0f);
        }
    for (ImDrawList *dl : dd->CmdLists)
        for (ImDrawVert &v : dl->VtxBuffer) {
            ImU32 c = v.col;
            v.col = (c & 0xff000000u) | (ImU32)lut[c >> 16 & 0xff] << 16 | (ImU32)lut[c >> 8 & 0xff] << 8 | lut[c & 0xff];
        }
}

extern "C" int menu_overlay_frame(uint64_t frame, const MenuTarget *t, const char **shot) {
    *shot = NULL;
    mlb_capture_tick(frame, shot);   // F12 / MLB_SHOT: this frame's picture to a file
    bool open = g_open.load(std::memory_order_relaxed);
    if (!open) {
        if (g_save_pending) g_save_pending = opt_save_tick(1);   // closed: the last change goes to mlb.cfg now
        // Test hook: MLB_MENU_TEST=<frame>[:<tab>] opens the menu at that presented frame (with MLB_SHOT, a picture of it)
        static long test_at = -2;
        if (test_at == -2) {
            const char *e = cfg_get("MLB_MENU_TEST");
            char *end;
            test_at = e && *e ? strtol(e, &end, 10) : -1;
            if (test_at >= 0 && *end == ':') g_tab_request = atoi(end + 1) % TAB_COUNT;
        }
        if (test_at < 0 || frame != (uint64_t)test_at) return 0;
        change_open(1);
        open = true;
    }
    if (g_failed.load(std::memory_order_relaxed)) {
        change_open(0);
        return 0;
    }
    if (!g_ctx_ok) make_context();
    float scale = menu_scale(t->height);
    if (!vk_up(t) || (g_font_scale != scale && !fonts_up(scale))) {
        rt_log("menu: Dear ImGui Vulkan back end set-up failed; the menu stays closed");
        g_failed.store(1);
        change_open(0);
        return 0;
    }
    if (g_style_scale != scale) {
        ImGui::GetStyle() = g_base_style;
        ImGui::GetStyle().ScaleAllSizes(scale);
        g_style_scale = scale;
    }
    ImGuiIO &io = ImGui::GetIO();
    int reset = g_reset.exchange(0);
    if (reset) {
        io.ClearInputKeys();
        io.ClearInputMouse();
        g_last_ns = 0;
        g_focus_page = g_tab_request >= 0 ? g_tab_request : g_tab;   // opening: the first row has the cursor
        if (GImGui->OpenPopupStack.Size) ImGui::ClosePopupToLevel(0, false);
    }
    uint64_t t0 = plat_now_ns();
    io.DisplaySize = ImVec2((float)t->width, (float)t->height);
    double dt = g_last_ns ? (t0 - g_last_ns) / 1e9 : 1 / 60.0;
    io.DeltaTime = (float)(dt < 1e-4 ? 1e-4 : dt > 0.1 ? 0.1 : dt);
    g_last_ns = t0;
    drain_events(io);
    pad_events(io, reset);
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    layout_menu(t->width, t->height, scale);
    ImGui::Render();
    if (srgb_format(t->format)) linear_colours(ImGui::GetDrawData());
    g_save_pending = opt_save_tick(0);
    return ImGui::GetDrawData()->TotalVtxCount > 0;
}

extern "C" void menu_overlay_draw(VkCommandBuffer cb) { ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb); }
