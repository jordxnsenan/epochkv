
#include "epochkv/replication/PrimaryReplicator.hpp"
#include "epochkv/replication/LogHistory.hpp"

#include <stdexcept>

namespace epochkv
{

    PrimaryReplicator::PrimaryReplicator(Database &database, const BackupClient &backup_client) : database_(database), backup_client_(backup_client)
    {
    }

    void PrimaryReplicator::put(const std::string &key, const std::string &value)
    {
        ensureReady();

        std::uint64_t sequence_number = database_.preparePut(key, value);

        replicatePrepared(sequence_number);
    }

    std::optional<std::string> PrimaryReplicator::get(const std::string &key) const
    {
        ensureReady();

        return database_.get(key);
    }

    bool PrimaryReplicator::erase(const std::string &key)
    {
        ensureReady();

        auto sequence_number = database_.prepareDelete(key);

        if (!sequence_number)
        {
            return false;
        }

        replicatePrepared(*sequence_number);

        return true;
    }

    void PrimaryReplicator::replicatePrepared(std::uint64_t sequence_number)
    {
        try
        {
            auto pending_operation = database_.pendingOperation();

            if (!pending_operation ||
                pending_operation->sequence_number != sequence_number)
            {
                throw std::runtime_error("Prepared operation is missing");
            }

            backup_client_.prepare(*pending_operation);

            database_.commitPrepared(sequence_number);

            backup_client_.commit(sequence_number);
        }
        catch (...)
        {
            needs_reconciliation_ = true;

            throw;
        }
    }

    void PrimaryReplicator::ensureReady() const
    {
        if (needs_reconciliation_)
        {
            throw std::runtime_error("Replication requires reconciliation before continuing");
        }
    }

    void PrimaryReplicator::reconcile()
    {
        needs_reconciliation_ = true;

        std::vector<LogRecord> primary_records = database_.history();
        std::vector<LogRecord> backup_records = backup_client_.history();

        verifyMatchingHistory(primary_records, backup_records);

        // replays records that the primary has persisted but the backup is missing
        for (std::size_t record_index = backup_records.size();
             record_index < primary_records.size();
             ++record_index)
        {
            const LogRecord &record = primary_records[record_index];

            if (record.type == LogRecordType::Commit)
            {
                backup_client_.commit(record.sequence_number);
            }

            else
            {
                backup_client_.prepare(record);
            }
        }

        // an interrupted write is deemed complete once backup has its preparation
        auto pending_operation = database_.pendingOperation();

        if (pending_operation)
        {
            std::uint64_t sequence_number = pending_operation->sequence_number;

            database_.commitPrepared(sequence_number);

            backup_client_.commit(sequence_number);
        }

        primary_records = database_.history();
        backup_records = backup_client_.history();

        verifyMatchingHistory(primary_records, backup_records);

        if (primary_records.size() != backup_records.size())
        {
            throw std::runtime_error("Backup is still missing log records");
        }

        needs_reconciliation_ = false;
    }

}