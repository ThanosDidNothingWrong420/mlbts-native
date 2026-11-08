// MLB The Show 26 1.0.15: figures from the update's control.nacp (tools/nxextract.py parse_nacp;
// docs/recon-report.md) that the generic runtime asks the game layer for.
#include "fs.h"

const FsTitle fs_title = {
    .program_id = 0x0100AA7024094000ull,   // also the save data owner id
    .save_size = 0x3E00000,                // user account save 62 MiB
    .save_journal = 0x500000,              // journal 5 MiB
    .save_size_max = 0x8600000,            // 134 MiB
    .save_journal_max = 0x500000,
    .temp_size = 0x11400000,               // temporary storage 276 MiB
};
