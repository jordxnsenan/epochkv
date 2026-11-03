#include "epochkv/replication/BackupStore.hpp"

#include <limits>
#include <stdexcept>

namespace epochkv
{
    namespace
    {

        bool sameOperation(const LogRecord &first, const LogRecord &second)
        {
            return first.type == second.type &&
                   first.sequence_number == second.sequence_number &&
                   first.key == second.key &&
                   first.value == second.value;
        }

        void validateRecord(const LogRecord &record)
        {
            if (record.sequence_number == 0)
            {
                throw std::invalid_argument("Sequence number must be positive");
            }

            if (record.type == LogRecordType::Commit)
            {
                if (!record.key.empty() || !record.value.empty())
                {
                    throw std::invalid_argument("Commit records cannot contain data");
                }

                return;
            }

            if (record.type != LogRecordType::Put && record.type != LogRecordType::Delete)
            {
                throw std::invalid_argument("Unknown record type");
            }

            if (record.key.empty() ||
                record.key.size() > MAX_KEY_BYTES ||
                record.value.size() > MAX_VALUE_BYTES ||
                (record.type == LogRecordType::Delete && !record.value.empty()))
            {
                throw std::invalid_argument("Invalid key or value in replication record");
            }
        }

    }

    BackupStore::BackupStore(const std::filesystem::path &log_path) : log_store_(log_path)
    {
        recover();
    }

    std::string BackupStore::processRecord(const LogRecord &record)
    {
        if (requires_restart_)
        {
            throw std::runtime_error("Backup must restart after a failed log write");
        }

        validateRecord(record);

        if (record.type == LogRecordType::Commit)
        {
            if (record.sequence_number <= last_committed_sequence_)
            {
                return "COMMITTED";
            }

            if (!pending_operation_ ||
                pending_operation_->sequence_number != record.sequence_number)
            {
                throw std::runtime_error("No matching prepared operation");
            }

            appendDurably(record);

            pending_operation_.reset();

            last_committed_sequence_ = record.sequence_number;

            return "COMMITTED";
        }

        if (pending_operation_)
        {
            if (sameOperation(*pending_operation_, record))
            {
                return "PREPARED";
            }

            throw std::runtime_error("Backup has a different pending operation");
        }

        if (last_committed_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
            record.sequence_number != last_committed_sequence_ + 1)
        {
            throw std::runtime_error("Unexpected replication sequence number");
        }

        appendDurably(record);

        pending_operation_ = record;

        return "PREPARED";
    }

    bool BackupStore::requiresRestart() const
    {
        return requires_restart_;
    }

    void BackupStore::recover()
    {
        for (const LogRecord &record : log_store_.readAll())
        {
            validateRecord(record);

            if (record.type == LogRecordType::Commit)
            {
                if (!pending_operation_ ||
                    pending_operation_->sequence_number != record.sequence_number)
                {
                    throw std::runtime_error("Commit without a matching prepare record");
                }

                last_committed_sequence_ = record.sequence_number;

                pending_operation_.reset();

                continue;
            }

            if (pending_operation_ ||
                last_committed_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
                record.sequence_number != last_committed_sequence_ + 1)
            {
                throw std::runtime_error("Invalid order of recovered replication records");
            }

            pending_operation_ = record;
        }
    }

    void BackupStore::appendDurably(const LogRecord &record)
    {
        try
        {
            log_store_.append(record);
        }

        catch (...)
        {
            requires_restart_ = true;

            throw;
        }
    }

}