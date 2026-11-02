#include "epochkv/Database.hpp"

#include <limits>
#include <optional>
#include <stdexcept>

namespace epochkv
{

    Database::Database(const std::filesystem::path &log_path)
        : log_store_(log_path)
    {
        recover();
    }

    void Database::put(const std::string &key, const std::string &value)
    {
        ensureReady();

        commitOperation({LogRecordType::Put, next_sequence_number_, key, value});
    }

    std::optional<std::string> Database::get(const std::string &key) const
    {
        ensureReady();

        return key_value_store_.get(key);
    }

    bool Database::erase(const std::string &key)
    {
        ensureReady();

        if (!key_value_store_.get(key).has_value())
        {
            return false;
        }

        commitOperation({LogRecordType::Delete, next_sequence_number_, key, ""});

        return true;
    }

    void Database::commitOperation(const LogRecord &operation)
    {
        if (next_sequence_number_ == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Log sequence numbers have been exhausted");
        }

        try
        {
            log_store_.append(operation);
            log_store_.append({LogRecordType::Commit, operation.sequence_number, "", ""});
        }
        catch (const std::invalid_argument &)
        {
            throw;
        }
        catch (...)
        {
            requires_restart_ = true;

            throw;
        }

        try
        {
            apply(operation);
        }
        catch (...)
        {
            requires_restart_ = true;

            throw;
        }

        next_sequence_number_++;
    }

    void Database::recover()
    {
        std::optional<LogRecord> pending_operation;

        for (const LogRecord &record : log_store_.readAll())
        {
            if (record.type == LogRecordType::Commit)
            {
                if (!pending_operation || pending_operation->sequence_number != record.sequence_number)
                {
                    throw std::runtime_error("Commit record does not match a pending operation");
                }

                apply(*pending_operation);

                pending_operation.reset();

                continue;
            }

            if (record.sequence_number < next_sequence_number_ ||
                record.sequence_number == std::numeric_limits<std::uint64_t>::max())
            {
                throw std::runtime_error("Log operation sequence numbers are invalid");
            }

            next_sequence_number_ = record.sequence_number + 1;

            pending_operation = record;
        }
    }

    void Database::apply(const LogRecord &operation)
    {
        if (operation.type == LogRecordType::Put)
        {
            key_value_store_.put(operation.key, operation.value);
        }
        else if (operation.type == LogRecordType::Delete)
        {
            key_value_store_.erase(operation.key);
        }
        else
        {
            throw std::runtime_error("Cannot apply an unknown database operation");
        }
    }

    void Database::ensureReady() const
    {
        if (requires_restart_)
        {
            throw std::runtime_error("Database must restart after a failed write");
        }
    }

}