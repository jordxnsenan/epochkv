#pragma once

#include "epochkv/storage/LogRecord.hpp"

#include <vector>

namespace epochkv
{

    void verifyMatchingHistory(
        const std::vector<LogRecord> &primary_history,
        const std::vector<LogRecord> &backup_history);

}
