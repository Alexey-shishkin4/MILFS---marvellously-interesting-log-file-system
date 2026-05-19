#pragma once

#include "fs_core.h"

struct GcStats {
    uint32_t cleaned_segments = 0;
    uint32_t moved_records = 0;
    uint32_t moved_data_records = 0;
    uint32_t skipped_records = 0;
};

FsError fs_gc_once(FileSystemState& fs, GcStats& stats);
