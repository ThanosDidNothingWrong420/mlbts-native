// Environment-backed settings (cfg.h). Defaults for the keys the core reads are listed here so that an unset key
// behaves as it does in TotK Recomp (runtime/cfg.c @ ae30a61, MIT, which keeps the full table).
#include "cfg.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *key, *def; } defaults[] = {
    {"TOTK_ASYNC_PREEMPT", "1"},   // kernel.c: asynchronous preemption of a core holder
    {"TOTK_SPIN", "1"},            // kernel.c: spin-wait gates (none are emitted for MLB yet)
    {"TOTK_FREE_CORES", "0"},      // kernel.c: experiments only
    {"MLB_LANGUAGE", "en-US"},     // am.c GetDesiredLanguage; the NACP lists American English only
};

void cfg_init(void) {}

const char *cfg_get(const char *name) {
    if (!strncmp(name, "TOTK_", 5)) {
        char mlb[128];
        size_t n = strlen(name + 5);
        if (n + 5 < sizeof mlb) {
            memcpy(mlb, "MLB_", 4);
            memcpy(mlb + 4, name + 5, n + 1);
            const char *v = getenv(mlb);
            if (v) return v;
        }
    }
    return getenv(name);
}

const char *cfg_value(const char *name) {
    const char *v = cfg_get(name);
    if (v) return v;
    for (size_t i = 0; i < sizeof defaults / sizeof *defaults; i++)
        if (!strcmp(defaults[i].key, name)) return defaults[i].def;
    return "";
}

int cfg_int(const char *name) { return atoi(cfg_value(name)); }

int cfg_bool(const char *name) {
    const char *v = cfg_value(name);
    char b[8] = {0};
    for (int i = 0; v[i] && i < 7; i++) b[i] = (char)tolower((unsigned char)v[i]);
    return !strcmp(b, "1") || !strcmp(b, "true") || !strcmp(b, "on") || !strcmp(b, "yes");
}
