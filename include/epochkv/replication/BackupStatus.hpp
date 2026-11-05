#pragma once

#include <cstdint>

namespace epochkv
{

    struct BackupStatus
    {
        std::uint64_t last_committed_sequence = 0;
        std::uint64_t pending_sequence = 0;
    };

}
