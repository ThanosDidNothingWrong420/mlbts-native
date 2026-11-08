// TotK Recomp graphics features the MLB port does not carry (docs/graphics-plan.md, "Not ported"): their entry
// points, as the vendored back end calls them, each switched off. Declarations in vk.h, vk_dlaa.h, gfx.h, flight.h,
// input.h, ui/menu.h and host.h (all from TotK Recomp @ ae30a61). A feature that comes back replaces its stubs here.
#include <stdint.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#include "cfg.h"
#include "flight.h"
#include "gfx.h"
#include "host.h"
#include "input.h"
#include "ui/menu.h"
#include "vk.h"
#include "vk_dlaa.h"
#include "vk_exec.h"

// ---- frame_rate.c: the host half of TotK's fps60/realtime patch sets. Off: presents follow the game's own NVN
// present interval.
int frame_step_realtime(void) { return 0; }
uint64_t frame_step_cap_ns(void) { return 0; }
void frame_step_present(uint64_t display_ns, int interval) { (void)display_ns, (void)interval; }

// ---- resolution.c: TotK's output/scene size patches and FOV
void res_fov_live(void) {}
void res_present(Cpu *c) { (void)c; }

// ---- ov_movie.c: TotK's VP9 movie decoder (MLB plays Bink; not ported yet)
void movie_tick(Cpu *c) { (void)c; }

// ---- perf.c: frame timing counters and the A/B knobs. Knobs read the setting as TotK's perf.c does.
uint64_t perf_begin(void) { return 0; }
void perf_end(int kind, uint64_t t0) { (void)kind, (void)t0; }
void perf_count(int kind) { (void)kind; }
void perf_present(uint64_t frame) { (void)frame; }
void perf_replay(uint64_t t0) { (void)t0; }
void perf_replay_thread(void) {}
void perf_gpu_busy(uint64_t ns) { (void)ns; }
void perf_gpu_wait(uint64_t t0) { (void)t0; }
int perf_gpu_timing(void) { return 0; }
void (*perf_gpu_stats)(PerfGpu *out);
int rt_ab(void) { return 0; }
int rt_knob(const char *name, const char *def) {   // TotK perf.c rt_knob: "ab" -> 2, "0" -> 0, anything else -> 1
    char key[64];
    strcpy(key, "TOTK_");
    strncat(key, name, sizeof key - 6);
    const char *e = cfg_get(key);
    if (!e || !*e) e = def;
    return !strcmp(e, "ab") ? 2 : strcmp(e, "0") != 0;
}

// ---- flight.c, rdoc.c, vk_crumbs.c, vk_dump.c, pass marks: diagnostics
int flight_on;
int rdoc_active(void) { return 0; }
void rdoc_init(void) {}
void rdoc_after_present(uint64_t frame) { (void)frame; }
void vk_crumb(VkCommandBuffer cb, int kind, uint64_t n) { (void)cb, (void)kind, (void)n; }
void vk_crumbs_init(VkCtx *c) { (void)c; }
void vk_crumbs_release(void) {}
void vk_crumbs_report(void) {}
int vk_dump_armed;
void vk_dump_init(void) {}
void vk_dump_copy(VkExec *ex, uint32_t op, const void *payload) { (void)ex, (void)op, (void)payload; }
void vk_dump_dispatch(VkExec *ex, const GfxShader *cs, VkPipeline p, const VkPipeInfo *info, const p_dispatch *d) {
    (void)ex, (void)cs, (void)p, (void)info, (void)d;
}
void vk_dump_draw(VkExec *ex, const VkPipeState *s, VkPipeline p, const VkPipeInfo *info) {
    (void)ex, (void)s, (void)p, (void)info;
}
void vk_dump_pass_end(VkExec *ex) { (void)ex; }
void vk_dump_present(uint64_t frame) { (void)frame; }
VkPassMark *vk_pass_mark_begin(VkCommandBuffer cb, int kind, uint64_t fence) { (void)cb, (void)kind, (void)fence; return NULL; }
void vk_pass_mark_end(VkCommandBuffer cb, const VkPassMark *m) { (void)cb, (void)m; }
void vk_pass_mark_cmd(VkCommandBuffer cb, uint32_t id, int end) { (void)cb, (void)id, (void)end; }
void vk_pass_marks_report(void) {}
int vk_pass_marks_want_nv(void) { return 0; }
void rt_write_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h) {
    (void)rgba, (void)w, (void)h;
    rt_log("gfx: screenshot %s not written (PNG writer not ported)", path);
}
int probe_census_dir(char *dir, size_t n) { (void)dir, (void)n; return 0; }

// ---- DLAA, motion vectors (vk_dlaa.c, vk_mvprev.c, vk_mvsurvey.c)
const VkTexImage *vk_dlaa_swap_img;
VkImageView vk_dlaa_swap_view;
int vk_dlaa_available(void) { return 0; }
void vk_dlaa_init(void) {}
void vk_dlaa_instance_exts(const char **ext, uint32_t *n, uint32_t max) { (void)ext, (void)n, (void)max; }
void vk_dlaa_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max) {
    (void)have, (void)nhave, (void)ext, (void)n, (void)max;
}
void vk_dlaa_frame(void) {}
void vk_dlaa_pass(VkExec *ex) { (void)ex; }
void vk_dlaa_draw(VkExec *ex) { (void)ex; }
int vk_dlaa_skip_draw(VkExec *ex, const GfxShader *fs) { (void)ex, (void)fs; return 0; }
void vk_dlaa_viewport(VkExec *ex, VkViewport *v) { (void)ex, (void)v; }
int vk_dlaa_prev_camera(VkDescriptorBufferInfo *out, float jitter[2]) { (void)out, (void)jitter; return 0; }
int vk_mvprev_on(void) { return 0; }
void vk_mvprev_draw(VkExec *ex) { (void)ex; }
void vk_mvprev_frame(void) {}
void vk_mvprev_want(uint32_t index_mask) { (void)index_mask; }
int vk_mvprev_find(int index, uint64_t addr, uint64_t size, uint64_t *prev, int *how) {
    (void)index, (void)addr, (void)size, (void)prev, (void)how;
    return 0;
}
uint64_t vk_mvprev_step(int stage, int index, uint64_t size) { (void)stage, (void)index, (void)size; return 0; }
void vk_mvsurvey_draw(VkExec *ex) { (void)ex; }
void vk_mvsurvey_frame(void) {}

// ---- contact shadows (vk_contact.c), FSR 1 (vk_fsr.c)
int vk_contact_strength(void) { return 0; }
void vk_contact_pass_begin(VkExec *ex) { (void)ex; }
void vk_contact_flush(VkExec *ex) { (void)ex; }
void vk_contact_release(void) {}
int vk_fsr_record(VkCommandBuffer cb, unsigned slot, VkImage src, VkFormat fmt, int sw, int sh, const int crop[4],
                  VkImage dst, const int out[4]) {
    (void)cb, (void)slot, (void)src, (void)fmt, (void)sw, (void)sh, (void)crop, (void)dst, (void)out;
    return 0;   // not recorded: the present blits
}
void vk_fsr_release(void) {}

// ---- HDR output (vk_hdr.c): SDR only
int vk_hdr_track;
int vk_hdr_wanted(void) { return 0; }
void vk_hdr_support(float out[4]) { memset(out, 0, 4 * sizeof *out); }
int vk_hdr_surface_format(const VkSurfaceFormatKHR *f, uint32_t n, VkSurfaceFormatKHR *out) {
    (void)f, (void)n, (void)out;
    return 0;
}
void vk_hdr_set_active(int on) { (void)on; }
void vk_hdr_release(void) {}
void vk_hdr_metadata(VkSwapchainKHR sc, int new_sc) { (void)sc, (void)new_sc; }
void vk_hdr_shader_swap(uint32_t **spirv, uint64_t *words) { (void)spirv, (void)words; }
void vk_hdr_draw(VkExec *ex, const VkPipeInfo *info) { (void)ex, (void)info; }
void vk_hdr_image_gone(VkImage image) { (void)image; }
void vk_hdr_present_source(const VkTexImage *win, VkImage *image, VkFormat *format) { (void)win, (void)image, (void)format; }
VkImage vk_hdr_encode(VkCommandBuffer cb, unsigned slot, VkImage src, VkFormat fmt, const int crop[4]) {
    (void)cb, (void)slot, (void)fmt, (void)crop;
    return src;
}
VkImageView vk_hdr_ui_target(VkCommandBuffer cb, uint32_t w, uint32_t h) { (void)cb, (void)w, (void)h; return VK_NULL_HANDLE; }
void vk_hdr_ui_compose(VkCommandBuffer cb, VkImageView dst_view, VkFormat fmt, uint32_t w, uint32_t h) {
    (void)cb, (void)dst_view, (void)fmt, (void)w, (void)h;
}
void vk_hdr_to_srgb8(const uint8_t *in, uint8_t *out, size_t px, int a2r) { (void)a2r; memcpy(out, in, px * 4); }

// ---- the in-game settings menu (ui/menu.cpp, ImGui) and keyboard/mouse input (input_win.c): not ported yet; the
// window procedure then handles every message itself.
void menu_overlay_draw(VkCommandBuffer cb) { (void)cb; }
int menu_overlay_frame(uint64_t frame, const MenuTarget *t, const char **shot) { (void)frame, (void)t, (void)shot; return 0; }
void menu_vk_release(void) {}
int menu_window_msg(HWND h, UINT m, WPARAM w, LPARAM l, LRESULT *res) { (void)h, (void)m, (void)w, (void)l, (void)res; return 0; }
int input_window_msg(HWND h, UINT m, WPARAM w, LPARAM l, LRESULT *res) { (void)h, (void)m, (void)w, (void)l, (void)res; return 0; }
void input_cursor_suspend(int on) { (void)on; }

// ---- boot.c --frames: every present lands here; log the first and then every 600th
void rt_frame_presented(const tex_t *t, uint64_t n) {
    (void)t;
    if (n == 1 || n % 600 == 0) rt_log("gfx: frame %llu presented", (unsigned long long)n);
}
