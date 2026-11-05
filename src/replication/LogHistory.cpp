#include "epochkv/replication/LogHistory.hpp"

#include <stdexcept>
#include <string>

namespace epochkv
{
    namespace
    {

        bool sameRecord(const LogRecord &first, const LogRecord &second)
        {
            return first.type == second.type &&
                   first.sequence_number == second.sequence_number &&
                   first.key == second.key &&
                   first.value == second.value;
        }

    }

    void verifyMatchingHistory(
        const std::vector<LogRecord> &primary_history,
        const std::vector<LogRecord> &backup_history)
    {
        if (backup_history.size() > primary_history.size())
        {
            throw std::runtime_error("Backup contains records missing from the primary");
        }

        for (std::size_t record_index = 0; record_index < backup_history.size(); record_index++)
        {
            if (!sameRecord(primary_history[record_index],
                            backup_history[record_index]))
            {
                throw std::runtime_error(
                    "Primary and backup histories differ at record index " +

                    std::to_string(record_index));
            }
        }
    }

}
