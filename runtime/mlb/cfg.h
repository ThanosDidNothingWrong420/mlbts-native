// Settings (cfg.c). Interface from TotK Recomp runtime/cfg.h @ ae30a61 (MIT): the settings file and the menu's
// save are not ported. Keys are TotK's names ("TOTK_X" or "X"); MLB_X in the environment wins over TOTK_X.
#pragma once

// What the table knows about a setting. key: without TOTK_, upper case. def: the value when unset ("" = the consumer
// picks). values: "0|1", "a|b|c", a range "0..300", or NULL for free text. live: applies at once.
typedef struct { const char *key, *def, *values; int live; } CfgMeta;

void cfg_init(void);                        // idempotent; memory.c calls it first thing
const char *cfg_get(const char *name);      // environment (MLB_, TOTK_), then cfg_set; NULL when unset
void cfg_set(const char *name, const char *value);   // for this run
unsigned cfg_version(void);                 // changes with every cfg_set
int cfg_is_set(const char *name);
int cfg_save(void);                         // no settings file: 0
const CfgMeta *cfg_meta(const char *key);
const char *cfg_value(const char *name);    // cfg_get, else the table's default; fatal for a key without a row
int cfg_int(const char *name);              // unparsable: the default
double cfg_double(const char *name);
int cfg_bool(const char *name);             // anything but "0"
