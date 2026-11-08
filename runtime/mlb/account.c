// The local user: one offline account whose Uid survives restarts in <profile>/account.uid (host.h account_uid).
// Adapted from TotK Recomp runtime/ov_system.c load_uid @ ae30a61 (MIT, LICENSE.totk-recomp).
//
// MLB's NACP requires a startup user account (StartupUserAccount = 1), so am hands this Uid out as the preselected
// user (PopLaunchParameter kind 2). nn::account itself is not served yet.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/platform.h"

static PlatMutex account_lock = PLAT_MUTEX_INIT;
static uint64_t uid_lo, uid_hi;

static void load_uid(void) {   // holding account_lock
    if (uid_lo || uid_hi) return;
    const char *dir = rt_config()->profile;
    char path[1100];
    plat_file_mkdir_p(dir);
    if (!plat_dir_exists(dir)) rt_fatal("account: cannot create %s", dir);
    if (snprintf(path, sizeof path, "%s/account.uid", dir) >= (int)sizeof path) rt_fatal("account: path too long");
    uint64_t v[2] = {0, 0};
    if (plat_file_size(path) <= 0) {
        do
            if (!plat_random(v, sizeof v)) rt_fatal("account: no random bytes for a new uid");
        while (!v[0] && !v[1]);
        if (!plat_file_write_atomic(path, v, sizeof v)) rt_fatal("account: cannot write %s", path);
        rt_log("account: new local account in %s", path);
    } else {
        uint64_t got = 0;
        uint8_t *b = plat_file_read(path, &got);
        if (b && got == sizeof v) memcpy(v, b, sizeof v);
        free(b);
        if (got != sizeof v || (!v[0] && !v[1])) rt_fatal("account: %s is not a local account identity (16 bytes)", path);
    }
    uid_lo = v[0];
    uid_hi = v[1];
}

void account_uid(uint64_t out[2]) {
    plat_mutex_lock(&account_lock);
    load_uid();
    out[0] = uid_lo;
    out[1] = uid_hi;
    plat_mutex_unlock(&account_lock);
}
