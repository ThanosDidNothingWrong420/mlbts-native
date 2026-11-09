// The local user: one offline account whose Uid survives restarts in <profile>/account.uid (host.h account_uid), and
// the nn::account functions main imports, by symbol override. Adapted from TotK Recomp runtime/ov_system.c (the
// nn::account section) @ ae30a61 (MIT, LICENSE.totk-recomp).
//
// MLB's NACP requires a startup user account (StartupUserAccount = 1), so am also hands this Uid out as the
// preselected user (PopLaunchParameter kind 2). User handles are made here ({Uid, token}, nn::account::UserHandle's
// 24 bytes), so no SDK account function may see one: every imported function that takes a handle is overridden. The
// network service account ones answer as an offline console with no linked Nintendo Account would, or stop with a
// named fatal where that answer is not known.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
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

// ---- nn::account overrides
#define ACCOUNT_R_INVALID 0x7c67u   // MacNativeGames' result for an unknown user handle or uid (TotK ov_system.c)
typedef struct { uint64_t lo, hi; } Uid;
typedef struct { Uid uid; uint64_t token; } UserHandle;
_Static_assert(sizeof(UserHandle) == 24, "nn::account::UserHandle");
static uint64_t tokens[64], next_token = 1;

static int same_uid(const Uid *u) { return u && u->lo == uid_lo && u->hi == uid_hi; }   // holding account_lock

static int valid_handle(const UserHandle *h) {   // holding account_lock
    if (!h || !same_uid(&h->uid)) return 0;
    for (int i = 0; i < 64; i++)
        if (tokens[i] && tokens[i] == h->token) return 1;
    return 0;
}

static void need_handle(Cpu *c, const char *fn, int arg) {
    plat_mutex_lock(&account_lock);
    int ok = valid_handle(arg_p(c, arg));
    plat_mutex_unlock(&account_lock);
    if (!ok) rt_fatal("nn::account::%s: not an open user handle", fn);
}

// void Initialize()
static void ov_account_initialize(Cpu *c) {
    (void)c;
    uint64_t uid[2];
    account_uid(uid);
}

// bool TryOpenPreselectedUser(UserHandle *out)
static void ov_open_user(Cpu *c) {
    UserHandle *out = arg_p(c, 0);
    if (!out) rt_fatal("nn::account::TryOpenPreselectedUser: null handle");
    plat_mutex_lock(&account_lock);
    load_uid();
    int ok = 0;
    for (int i = 0; i < 64 && !ok; i++)
        if (!tokens[i]) {
            tokens[i] = next_token++;
            *out = (UserHandle){{uid_lo, uid_hi}, tokens[i]};
            ok = 1;
        }
    plat_mutex_unlock(&account_lock);
    ret_x(c, (uint64_t)ok);
}

// void CloseUser(const UserHandle &)
static void ov_close_user(Cpu *c) {
    const UserHandle *h = arg_p(c, 0);
    plat_mutex_lock(&account_lock);
    if (!valid_handle(h)) rt_fatal("nn::account::CloseUser: not an open user handle");
    for (int i = 0; i < 64; i++)
        if (tokens[i] == h->token) tokens[i] = 0;
    plat_mutex_unlock(&account_lock);
}

// Result GetUserId(Uid *out, const UserHandle &)
static void ov_user_id(Cpu *c) {
    Uid *out = arg_p(c, 0);
    plat_mutex_lock(&account_lock);
    int ok = out && valid_handle(arg_p(c, 1));
    if (ok) *out = (Uid){uid_lo, uid_hi};
    plat_mutex_unlock(&account_lock);
    ret_x(c, ok ? 0 : ACCOUNT_R_INVALID);
}

// Result GetNickname(Nickname *out, const Uid &): 33 bytes, NUL-terminated UTF-8; MLB_NICKNAME, default "Player"
static void ov_nickname(Cpu *c) {
    char *out = arg_p(c, 0);
    plat_mutex_lock(&account_lock);
    load_uid();
    int ok = out && same_uid(arg_p(c, 1));
    plat_mutex_unlock(&account_lock);
    if (ok) {
        memset(out, 0, 33);
        snprintf(out, 33, "%s", cfg_value("MLB_NICKNAME"));
    }
    ret_x(c, ok ? 0 : ACCOUNT_R_INVALID);
}

// Result IsNetworkServiceAccountAvailable(bool *out, const UserHandle &): no Nintendo Account is linked offline
static void ov_nsa_available(Cpu *c) {
    uint8_t *out = arg_p(c, 0);
    need_handle(c, "IsNetworkServiceAccountAvailable", 1);
    if (out) *out = 0;
    ret_x(c, out ? 0 : ACCOUNT_R_INVALID);
}

// The user lists. One local account: it always exists, is qualified for this application, and is open while any
// handle from TryOpenPreselectedUser is (the SDK's own friends code asks ListOpenUsers, so these are reached from
// sdk as well as main; the SDK versions would read an account service object that is never set up here).
static int open_count(void) {   // holding account_lock
    int n = 0;
    for (int i = 0; i < 64; i++) n += tokens[i] != 0;
    return n;
}

static void list_users(Cpu *c, int only_open) {   // Result (int *count, Uid *out, int max)
    int32_t *count = arg_p(c, 0);
    Uid *out = arg_p(c, 1);
    int max = (int)arg_x(c, 2);
    plat_mutex_lock(&account_lock);
    load_uid();
    int n = (!only_open || open_count()) && max > 0 ? 1 : 0;
    if (n && out) out[0] = (Uid){uid_lo, uid_hi};
    plat_mutex_unlock(&account_lock);
    if (count) *count = n;
    ret_x(c, 0);
}

// Result ListOpenUsers(int *, Uid *, int) and the overload with an arp::ApplicationInstanceId
static void ov_list_open(Cpu *c) { list_users(c, 1); }
// Result ListAllUsers(int *, Uid *, int); ListQualifiedUsers(int *, Uid *, int[, const ApplicationId &])
static void ov_list_all(Cpu *c) { list_users(c, 0); }

// Result GetUserCount(int *)
static void ov_user_count(Cpu *c) {
    int32_t *n = arg_p(c, 0);
    if (n) *n = 1;
    ret_x(c, 0);
}

// Result GetUserExistence(bool *, const Uid &)
static void ov_user_exists(Cpu *c) {
    uint8_t *out = arg_p(c, 0);
    plat_mutex_lock(&account_lock);
    load_uid();
    int yes = same_uid(arg_p(c, 1));
    plat_mutex_unlock(&account_lock);
    if (out) *out = (uint8_t)yes;
    ret_x(c, 0);
}

// Result GetLastOpenedUser(Uid *)
static void ov_last_opened(Cpu *c) {
    Uid *out = arg_p(c, 0);
    plat_mutex_lock(&account_lock);
    load_uid();
    if (out) *out = (Uid){uid_lo, uid_hi};
    plat_mutex_unlock(&account_lock);
    ret_x(c, 0);
}

// The online ones: with IsNetworkServiceAccountAvailable false the game should not get here; if it does, the answer
// Horizon gives (and whether an applet would show) is not known yet.
#define ACCOUNT_ONLINE(fn, name)                                                                                           static void fn(Cpu *c) {                                                                                                 (void)c;                                                                                                             rt_fatal("nn::account::%s: the network service account is not served (offline)", name);                         }
ACCOUNT_ONLINE(ov_nsa_id, "GetNetworkServiceAccountId")
ACCOUNT_ONLINE(ov_nsa_ensure, "EnsureNetworkServiceAccountAvailable")
ACCOUNT_ONLINE(ov_nsa_token, "LoadNetworkServiceAccountIdTokenCache")
ACCOUNT_ONLINE(ov_nsa_license, "ShowLicenseRequirementsForNetworkService")
ACCOUNT_ONLINE(ov_na_request, "CreateNintendoAccountAuthorizationRequest")
ACCOUNT_ONLINE(ov_nsa_token_async, "EnsureNetworkServiceAccountIdTokenCacheAsync")

HOST_OVERRIDE("_ZN2nn7account10InitializeEv", ov_account_initialize);
HOST_OVERRIDE("_ZN2nn7account22TryOpenPreselectedUserEPNS0_10UserHandleE", ov_open_user);
HOST_OVERRIDE("_ZN2nn7account9CloseUserERKNS0_10UserHandleE", ov_close_user);
HOST_OVERRIDE("_ZN2nn7account9GetUserIdEPNS0_3UidERKNS0_10UserHandleE", ov_user_id);
HOST_OVERRIDE("_ZN2nn7account11GetNicknameEPNS0_8NicknameERKNS0_3UidE", ov_nickname);
HOST_OVERRIDE("_ZN2nn7account32IsNetworkServiceAccountAvailableEPbRKNS0_10UserHandleE", ov_nsa_available);
HOST_OVERRIDE("_ZN2nn7account13ListOpenUsersEPiPNS0_3UidEi", ov_list_open);
HOST_OVERRIDE("_ZN2nn7account13ListOpenUsersEPiPNS0_3UidEiRKNS_3arp21ApplicationInstanceIdE", ov_list_open);
HOST_OVERRIDE("_ZN2nn7account12ListAllUsersEPiPNS0_3UidEi", ov_list_all);
HOST_OVERRIDE("_ZN2nn7account18ListQualifiedUsersEPiPNS0_3UidEi", ov_list_all);
HOST_OVERRIDE("_ZN2nn7account18ListQualifiedUsersEPiPNS0_3UidEiRKNS_13ApplicationIdE", ov_list_all);
HOST_OVERRIDE("_ZN2nn7account12GetUserCountEPi", ov_user_count);
HOST_OVERRIDE("_ZN2nn7account16GetUserExistenceEPbRKNS0_3UidE", ov_user_exists);
HOST_OVERRIDE("_ZN2nn7account17GetLastOpenedUserEPNS0_3UidE", ov_last_opened);
HOST_OVERRIDE("_ZN2nn7account26GetNetworkServiceAccountIdEPNS0_23NetworkServiceAccountIdERKNS0_10UserHandleE", ov_nsa_id);
HOST_OVERRIDE("_ZN2nn7account36EnsureNetworkServiceAccountAvailableERKNS0_10UserHandleE", ov_nsa_ensure);
HOST_OVERRIDE("_ZN2nn7account37LoadNetworkServiceAccountIdTokenCacheEPmPcmRKNS0_10UserHandleE", ov_nsa_token);
HOST_OVERRIDE("_ZN2nn7account40ShowLicenseRequirementsForNetworkServiceERKNS0_10UserHandleE", ov_nsa_license);
HOST_OVERRIDE("_ZN2nn7account41CreateNintendoAccountAuthorizationRequestEPNS0_42NintendoAccountAuthorizationRequestContextERKNS0_10UserHandleERKNS0_45NintendoAccountAuthorizationRequestParametersEPvm", ov_na_request);
HOST_OVERRIDE("_ZN2nn7account44EnsureNetworkServiceAccountIdTokenCacheAsyncEPNS0_12AsyncContextERKNS0_10UserHandleE", ov_nsa_token_async);
