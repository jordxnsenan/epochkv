#include "epochkv/Database.hpp"

#include <limits>
#include <stdexcept>

namespace epochkv
{

    Database::Database(const std::filesystem::path &log_path) : log_store_(log_path)
    {
        recover();
    }

    void Database::put(const std::string &key, const std::string &value)
    {
        std::uint64_t sequence_number = preparePut(key, value);

        commitPrepared(sequence_number);
    }

    std::optional<std::string> Database::get(const std::string &key) const
    {
        ensureReady();

        return key_value_store_.get(key);
    }

    bool Database::erase(const std::string &key)
    {
        auto sequence_number = prepareDelete(key);

        if (!sequence_number)
        {
            return false;
        }

        commitPrepared(*sequence_number);

        return true;
    }

    std::uint64_t Database::preparePut(const std::string &key, const std::string &value)
    {
        return prepareOperation({LogRecordType::Put, next_sequence_number_, key, value});
    }

    std::optional<std::uint64_t> Database::prepareDelete(const std::string &key)
    {
        ensureReady();

        if (pending_operation_)
        {
            throw std::logic_error("Database has an unfinished operation");
        }

        if (!key_value_store_.get(key).has_value())
        {
            return std::nullopt;
        }

        return prepareOperation({LogRecordType::Delete, next_sequence_number_, key, ""});
    }

    std::uint64_t Database::prepareOperation(const LogRecord &operation)
    {
        ensureReady();

        if (pending_operation_)
        {
            throw std::logic_error("Database has an unfinished operation");
        }

        if (next_sequence_number_ == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Log sequence numbers have been exhausted");
        }

        try
        {
            log_store_.append(operation);
            pending_operation_ = operation;
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

        next_sequence_number_++;

        return operation.sequence_number;
    }

    void Database::commitPrepared(std::uint64_t sequence_number)
    {
        ensureReady();

        if (!pending_operation_ ||
            pending_operation_->sequence_number != sequence_number)
        {
            throw std::logic_error("No matching prepared operation");
        }

        try
        {
            log_store_.append({LogRecordType::Commit, sequence_number, "", ""});

            apply(*pending_operation_);

            last_committed_sequence_ = sequence_number;

            pending_operation_.reset();
        }

        catch (...)
        {
            requires_restart_ = true;

            throw;
        }
    }

    std::optional<LogRecord> Database::pendingOperation() const
    {
        ensureReady();

        return pending_operation_;
    }

    std::uint64_t Database::lastCommittedSequence() const
    {
        ensureReady();

        return last_committed_sequence_;
    }

    void Database::recover()
    {
        for (const LogRecord &record : log_store_.readAll())
        {
            if (record.type == LogRecordType::Commit)
            {
                if (!pending_operation_ ||
                    pending_operation_->sequence_number != record.sequence_number)
                {
                    throw std::runtime_error("Commit record does not match a pending operation");
                }

                apply(*pending_operation_);

                last_committed_sequence_ = record.sequence_number;

                pending_operation_.reset();

                continue;
            }

            if (pending_operation_ ||
                record.sequence_number != next_sequence_number_ ||
                next_sequence_number_ == std::numeric_limits<std::uint64_t>::max())
            {
                throw std::runtime_error("Invalid operation sequence in database log");
            }

            pending_operation_ = record;

            next_sequence_number_++;
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

    std::vector<LogRecord> Database::history()
    {
        ensureReady();
        
        return log_store_.readAll();
    }

}