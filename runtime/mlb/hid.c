// nn::hid by symbol override: the functions main imports (docs/runtime-port.md), fed by one host controller. Our own
// implementation; state layouts and bit numbers per libnx hid.h (ISC), read for the layout only; the override approach
// and the controller support result after TotK Recomp runtime/ov_input.c @ ae30a61 (MIT).
//
// The console is docked (am.c), so the controller is a full-key controller as player 1 (NpadId No1, style bit 0); the
// handheld id and the Joy-Con styles read as disconnected. Its state is merged from:
//   - an XInput gamepad (xinput1_4.dll, loaded at run time; Xbox and compatible pads). MLB_PAD_MAP=label (default)
//     maps by label: Xbox A is Switch A (confirm), B is B; =position maps by place, as on the console: Xbox B (east) is
//     Switch A. Triggers past half way are ZL/ZR.
//   - the keyboard, while a window of this process is in the foreground (GetAsyncKeyState):
//       left stick W A S D, right stick I J K L, d-pad arrow keys, A Enter or Space, B Escape or Backspace, X R, Y F,
//       L Q, R E, ZL 1, ZR 3, + Tab, - M, left stick click C, right stick click V.
// Vibration plays on the XInput pad's motors (MLB_RUMBLE=0: off): the low band's amplitude on the left motor, the high
// band's on the right. Nothing here allocates guest memory or calls back into the guest.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"

// ---- libnx layouts
typedef struct { uint64_t sampling, buttons; int32_t lx, ly, rx, ry; uint32_t attributes, reserved; } NpadState;
typedef struct { int8_t players; uint8_t pad[3]; uint32_t selected; } ControllerSupportResult;
typedef struct { float amp_low, freq_low, amp_high, freq_high; } VibrationValue;
typedef struct { uint32_t type, position; } VibrationDeviceInfo;
_Static_assert(sizeof(NpadState) == 40, "nn::hid::NpadFullKeyState");
_Static_assert(sizeof(ControllerSupportResult) == 8 && sizeof(VibrationValue) == 16, "nn::hid layouts");

enum { STYLE_FULL_KEY = 1u << 0 };
enum { ATTR_CONNECTED = 1u << 0, ATTR_WIRED = 1u << 1 };
enum { ID_NO1 = 0, ID_HANDHELD = 0x20 };
enum {
    B_A, B_B, B_X, B_Y, B_LSTICK, B_RSTICK, B_L, B_R, B_ZL, B_ZR, B_PLUS, B_MINUS, B_LEFT, B_UP, B_RIGHT, B_DOWN,
    B_LS_LEFT, B_LS_UP, B_LS_RIGHT, B_LS_DOWN, B_RS_LEFT, B_RS_UP, B_RS_RIGHT, B_RS_DOWN,
};
#define BIT(n) (1ull << (n))
#define STICK_MAX 32767
#define STICK_DIR 16384   // the console sets the stick direction bits past half way

static PlatMutex lock = PLAT_MUTEX_INIT;
static int initialized;
static uint32_t supported_styles = STYLE_FULL_KEY;

// ---- XInput, loaded at run time
typedef struct { uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry; } XPad;
typedef struct { uint32_t packet; XPad pad; } XState;
typedef struct { uint16_t left, right; } XVibration;
static DWORD(WINAPI *x_get)(DWORD, XState *);
static DWORD(WINAPI *x_set)(DWORD, XVibration *);
static PlatOnce xinput_once = PLAT_ONCE_INIT;

static void xinput_load(void *arg) {
    (void)arg;
    HMODULE m = LoadLibraryA("xinput1_4.dll");
    if (!m) m = LoadLibraryA("xinput9_1_0.dll");
    if (m) {
        x_get = (DWORD(WINAPI *)(DWORD, XState *))(void *)GetProcAddress(m, "XInputGetState");
        x_set = (DWORD(WINAPI *)(DWORD, XVibration *))(void *)GetProcAddress(m, "XInputSetState");
    }
    rt_log("hid: XInput %s; keyboard while the game window has focus", x_get ? "loaded" : "not available");
}

static int pad_slot = -1;   // the XInput user index read last, -1 none

static int xinput_read(XPad *out) {
    plat_once(&xinput_once, xinput_load, NULL);
    if (!x_get) return 0;
    XState s;
    if (pad_slot >= 0 && x_get((DWORD)pad_slot, &s) == ERROR_SUCCESS) {
        *out = s.pad;
        return 1;
    }
    for (int i = 0; i < 4; i++)
        if (x_get((DWORD)i, &s) == ERROR_SUCCESS) {
            if (pad_slot != i) rt_log("hid: XInput pad %d", i);
            pad_slot = i;
            *out = s.pad;
            return 1;
        }
    pad_slot = -1;
    return 0;
}

static int32_t clamp_stick(int32_t v) { return v > STICK_MAX ? STICK_MAX : v < -STICK_MAX ? -STICK_MAX : v; }

static int window_focused(void) {
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return w && pid == GetCurrentProcessId();
}

static int key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// The merged controller state: buttons and sticks.
static void poll(NpadState *st) {
    uint64_t b = 0;
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    XPad p;
    if (xinput_read(&p)) {
        int label = strcmp(cfg_value("MLB_PAD_MAP"), "position") != 0;
        static const struct { uint16_t x; int lab, pos; } face[] = {
            {0x1000, B_A, B_B}, {0x2000, B_B, B_A}, {0x4000, B_X, B_Y}, {0x8000, B_Y, B_X},   // Xbox A B X Y
        };
        for (int i = 0; i < 4; i++)
            if (p.buttons & face[i].x) b |= BIT(label ? face[i].lab : face[i].pos);
        static const struct { uint16_t x; int bit; } other[] = {
            {0x0001, B_UP}, {0x0002, B_DOWN}, {0x0004, B_LEFT}, {0x0008, B_RIGHT}, {0x0010, B_PLUS}, {0x0020, B_MINUS},
            {0x0040, B_LSTICK}, {0x0080, B_RSTICK}, {0x0100, B_L}, {0x0200, B_R},
        };
        for (size_t i = 0; i < sizeof other / sizeof *other; i++)
            if (p.buttons & other[i].x) b |= BIT(other[i].bit);
        if (p.lt > 127) b |= BIT(B_ZL);
        if (p.rt > 127) b |= BIT(B_ZR);
        lx = clamp_stick(p.lx), ly = clamp_stick(p.ly), rx = clamp_stick(p.rx), ry = clamp_stick(p.ry);
    }
    if (window_focused()) {
        static const struct { int vk, bit; } keys[] = {
            {VK_RETURN, B_A}, {VK_SPACE, B_A}, {VK_ESCAPE, B_B}, {VK_BACK, B_B}, {'R', B_X}, {'F', B_Y}, {'Q', B_L},
            {'E', B_R}, {'1', B_ZL}, {'3', B_ZR}, {VK_TAB, B_PLUS}, {'M', B_MINUS}, {'C', B_LSTICK}, {'V', B_RSTICK},
            {VK_LEFT, B_LEFT}, {VK_UP, B_UP}, {VK_RIGHT, B_RIGHT}, {VK_DOWN, B_DOWN},
        };
        for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
            if (key(keys[i].vk)) b |= BIT(keys[i].bit);
        int kx = key('D') - key('A'), ky = key('W') - key('S');
        int jx = key('L') - key('J'), jy = key('I') - key('K');
        if (kx || ky) lx = kx * STICK_MAX, ly = ky * STICK_MAX;
        if (jx || jy) rx = jx * STICK_MAX, ry = jy * STICK_MAX;
    }
    if (lx <= -STICK_DIR) b |= BIT(B_LS_LEFT);
    if (lx >= STICK_DIR) b |= BIT(B_LS_RIGHT);
    if (ly >= STICK_DIR) b |= BIT(B_LS_UP);
    if (ly <= -STICK_DIR) b |= BIT(B_LS_DOWN);
    if (rx <= -STICK_DIR) b |= BIT(B_RS_LEFT);
    if (rx >= STICK_DIR) b |= BIT(B_RS_RIGHT);
    if (ry >= STICK_DIR) b |= BIT(B_RS_UP);
    if (ry <= -STICK_DIR) b |= BIT(B_RS_DOWN);
    st->buttons = b;
    st->lx = lx, st->ly = ly, st->rx = rx, st->ry = ry;
}

static uint32_t id_of(Cpu *c, int arg, const char *fn) {
    const uint32_t *id = arg_p(c, arg);
    if (!id) rt_fatal("nn::hid::%s: null NpadId", fn);
    return *id;
}

static int connected(uint32_t id) {
    plat_mutex_lock_shared(&lock);
    int on = initialized && id == ID_NO1 && (supported_styles & STYLE_FULL_KEY);
    plat_mutex_unlock_shared(&lock);
    return on;
}

// ---- overrides
// void InitializeNpad()
static void ov_initialize_npad(Cpu *c) {
    (void)c;
    plat_mutex_lock(&lock);
    initialized = 1;
    plat_mutex_unlock(&lock);
    rt_log("hid: one full-key controller as player 1");
}

// void SetSupportedNpadIdType(const NpadIdType *ids, size_t count)
static void ov_supported_ids(Cpu *c) {
    const uint32_t *ids = arg_p(c, 0);
    uint64_t n = arg_x(c, 1);
    if (n > 10 || (!ids && n)) rt_fatal("nn::hid::SetSupportedNpadIdType: %llu ids", (unsigned long long)n);
    int no1 = 0;
    for (uint64_t i = 0; i < n; i++) no1 |= ids[i] == ID_NO1;
    if (!no1) rt_log("hid: the game does not list NpadId No1 as supported; the controller stays player 1");
}

// void SetSupportedNpadStyleSet(NpadStyleSet styles): a 32-bit flag set in w0
static void ov_set_supported_styles(Cpu *c) {
    plat_mutex_lock(&lock);
    supported_styles = (uint32_t)arg_x(c, 0);
    plat_mutex_unlock(&lock);
    if (!(supported_styles & STYLE_FULL_KEY))
        rt_log("hid: the game does not support the full-key style (%#x): the controller reads as disconnected",
               supported_styles);
}

// NpadStyleSet GetSupportedNpadStyleSet()
static void ov_get_supported_styles(Cpu *c) {
    plat_mutex_lock_shared(&lock);
    ret_x(c, supported_styles);
    plat_mutex_unlock_shared(&lock);
}

// NpadStyleSet GetNpadStyleSet(const NpadIdType &id)
static void ov_style_set(Cpu *c) { ret_x(c, connected(id_of(c, 0, "GetNpadStyleSet")) ? STYLE_FULL_KEY : 0); }

// void GetNpadState(NpadXxxState *out, const NpadIdType &id): the latest sample. The full-key state is the
// controller's; the other styles' states read as disconnected (all zero but the sampling number).
static void npad_state(Cpu *c, int full_key, const char *fn) {
    NpadState *out = arg_p(c, 0);
    if (!out) rt_fatal("nn::hid::%s: null state", fn);
    uint32_t id = id_of(c, 1, fn);
    NpadState st;
    memset(&st, 0, sizeof st);
    st.sampling = kern_now_ns() / 5000000;   // the console samples every 5 ms
    if (full_key && connected(id)) {
        poll(&st);
        st.attributes = ATTR_CONNECTED | ATTR_WIRED;
    }
    *out = st;
}
static void ov_state_full_key(Cpu *c) { npad_state(c, 1, "GetNpadState(FullKey)"); }
static void ov_state_other(Cpu *c) { npad_state(c, 0, "GetNpadState"); }

// void SetNpadJoyHoldType(NpadJoyHoldType): only matters for single Joy-Cons
static void ov_hold_type(Cpu *c) { (void)c; }

// void InitializeTouchScreen(): no touch input on this host
static void ov_initialize_touch(Cpu *c) { (void)c; }

// int GetTouchScreenStates<1>(TouchScreenState<1> *out, int count): one sample with no touches. TouchScreenState<1> is
// {u64 sampling number, s32 touch count, u32 reserved, TouchState[1] (0x28 bytes)} (libnx HidTouchScreenState).
static void ov_touch_states(Cpu *c) {
    uint8_t *out = arg_p(c, 0);
    int n = (int)arg_x(c, 1);
    if (!out || n < 1) { ret_x(c, 0); return; }
    memset(out, 0, 0x38);
    uint64_t sampling = kern_now_ns() / 5000000;
    memcpy(out, &sampling, 8);
    ret_x(c, 1);
}

// Result ShowControllerSupport(ControllerSupportResultInfo *out, const ControllerSupportArg &): the applet would let
// the player pick controllers; one is always connected, so it returns at once with player 1 (TotK ov_input.c)
static void ov_controller_support(Cpu *c) {
    ControllerSupportResult *out = arg_p(c, 0);
    if (out) *out = (ControllerSupportResult){1, {0}, ID_NO1};
    rt_log("hid: controller support applet answered with one controller");
    ret_x(c, 0);
}

// ---- vibration: two devices (left, right motor) on the controller. A handle is {type, player, device, pad}.
enum { VIB_TYPE_FULL_KEY = 3 };
static float vib_low, vib_high;

// int GetVibrationDeviceHandles(VibrationDeviceHandle *out, int n, const NpadIdType &id, NpadStyleSet style)
static void ov_vib_handles(Cpu *c) {
    uint8_t *out = arg_p(c, 0);
    int n = (int)arg_x(c, 1);
    uint32_t id = id_of(c, 2, "GetVibrationDeviceHandles");
    int k = 0;
    for (; out && k < n && k < 2; k++) {
        uint8_t h[4] = {VIB_TYPE_FULL_KEY, (uint8_t)id, (uint8_t)k, 0};
        memcpy(out + 4 * k, h, 4);
    }
    ret_x(c, (uint64_t)k);
}

// void InitializeVibrationDevice(const VibrationDeviceHandle &)
static void ov_vib_init(Cpu *c) { (void)c; }

// void GetVibrationDeviceInfo(VibrationDeviceInfo *out, const VibrationDeviceHandle &): linear resonant actuators,
// left and right
static void ov_vib_info(Cpu *c) {
    VibrationDeviceInfo *out = arg_p(c, 0);
    const uint8_t *h = arg_p(c, 1);
    if (out && h) *out = (VibrationDeviceInfo){1, h[2] ? 2u : 1u};
}

// void SendVibrationValue(const VibrationDeviceHandle &, const VibrationValue &)
static void ov_vib_send(Cpu *c) {
    const uint8_t *h = arg_p(c, 0);
    const VibrationValue *v = arg_p(c, 1);
    if (!h || !v || !cfg_bool("MLB_RUMBLE")) return;
    plat_mutex_lock(&lock);
    if (h[2]) vib_high = v->amp_high > v->amp_low ? v->amp_high : v->amp_low;
    else vib_low = v->amp_low > v->amp_high ? v->amp_low : v->amp_high;
    float lo = vib_low, hi = vib_high;
    plat_mutex_unlock(&lock);
    plat_once(&xinput_once, xinput_load, NULL);
    if (x_set && pad_slot >= 0) {
        XVibration xv = {(uint16_t)(fminf(fmaxf(lo, 0), 1) * 65535), (uint16_t)(fminf(fmaxf(hi, 0), 1) * 65535)};
        x_set((DWORD)pad_slot, &xv);
    }
}

HOST_OVERRIDE("_ZN2nn3hid14InitializeNpadEv", ov_initialize_npad);
HOST_OVERRIDE("_ZN2nn3hid22SetSupportedNpadIdTypeEPKjm", ov_supported_ids);
HOST_OVERRIDE("_ZN2nn3hid24SetSupportedNpadStyleSetENS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", ov_set_supported_styles);
HOST_OVERRIDE("_ZN2nn3hid24GetSupportedNpadStyleSetEv", ov_get_supported_styles);
HOST_OVERRIDE("_ZN2nn3hid15GetNpadStyleSetERKj", ov_style_set);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj", ov_state_full_key);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj", ov_state_other);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj", ov_state_other);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj", ov_state_other);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj", ov_state_other);
HOST_OVERRIDE("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE", ov_hold_type);
HOST_OVERRIDE("_ZN2nn3hid21InitializeTouchScreenEv", ov_initialize_touch);
HOST_OVERRIDE("_ZN2nn3hid20GetTouchScreenStatesILm1EEEiPNS0_16TouchScreenStateIXT_EEEi", ov_touch_states);
HOST_OVERRIDE("_ZN2nn3hid21ShowControllerSupportEPNS0_27ControllerSupportResultInfoERKNS0_20ControllerSupportArgE",
              ov_controller_support);
HOST_OVERRIDE("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_"
              "12NpadStyleTagEEE", ov_vib_handles);
HOST_OVERRIDE("_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", ov_vib_init);
HOST_OVERRIDE("_ZN2nn3hid22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE", ov_vib_info);
HOST_OVERRIDE("_ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", ov_vib_send);
