// mlbts-native: Xbox and PlayStation button icons in the game's prompts (prompts.c). No Vulkan here: vk_texture.c
// records the copies.
#pragma once
#include <stddef.h>
#include <stdint.h>

// A run of BC7 blocks along one row of the icon texture: first block at (bx, by) in blocks, n blocks of 16 bytes.
typedef struct { uint16_t bx, by, n; const uint8_t *blocks; } MlbPromptRun;

// 1 when a texture's shape is the icon texture's (1024x1024 BC7 sRGB, one level): worth a look at its bytes.
int mlb_prompts_shape(int nvn_format, uint32_t width, uint32_t height, uint32_t levels);
// 1 when the storage (block-linear, as the game wrote it) holds the game's button icons. Logs the first other
// textures of that shape it is asked about.
int mlb_prompts_is_icons(uint64_t addr, const void *storage, size_t size);
// The icon set the prompts should show now: -1 the game's own (Switch), else a set index for mlb_prompts_runs.
int mlb_prompts_set(void);
// The runs of a set, and the number of blocks in them.
int mlb_prompts_runs(int set, const MlbPromptRun **runs, int *blocks);

// hid.c: the kind of controller in use, for MLB_PROMPTS=auto: 0 none or other (the game's own icons), 1 Xbox,
// 2 PlayStation.
int hid_prompt_kind(void);
