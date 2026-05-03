#pragma once

#include "fs_core.h"

namespace recovery {

FsError replay_log(FileSystemState& fs);

} // namespace recovery
