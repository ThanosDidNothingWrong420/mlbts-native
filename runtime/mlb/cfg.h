// Settings for the runtime core (runtime/core). The core asks for TotK Recomp's key names (TOTK_TRACE, TOTK_SPIN,
// ...); cfg_get answers them from the environment, where MLB_<rest> wins over TOTK_<rest>. Interface after TotK
// Recomp runtime/cfg.h @ ae30a61 (MIT); the settings file and the in-game menu are not ported.
#pragma once

void cfg_init(void);                       // idempotent; memory.c calls it first thing
const char *cfg_get(const char *name);     // NULL when unset
const char *cfg_value(const char *name);   // cfg_get, or the built-in default ("" when there is none)
int cfg_int(const char *name);             // atoi(cfg_value), 0 when unset
int cfg_bool(const char *name);            // "1", "true", "on", "yes" (case-insensitive)
