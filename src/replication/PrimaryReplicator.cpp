
#include "epochkv/replication/PrimaryReplicator.hpp"
#include "epochkv/replication/LogHistory.hpp"

#include <stdexcept>
#include <chrono>

namespace epochkv
{

    namespace
    {

        using Clock = std::chrono::steady_clock;

        double elapsedMilliseconds(Clock::time_point start, Clock::time_point end)
        {
            return std::chrono::duration<double, std::milli>(end - start).count();
        }

    }

    PrimaryReplicator::PrimaryReplicator(Database &database, const BackupClient &backup_client) : database_(database), backup_client_(backup_client)
    {
    }

    void PrimaryReplicator::put(const std::string &key, const std::string &value)
    {
        ensureReady();
        last_write_timing_.reset();

        auto start = Clock::now();
        std::uint64_t sequence_number = database_.preparePut(key, value);

        ReplicationWriteTiming timing;
        timing.primary_prepare_ms = elapsedMilliseconds(start, Clock::now());

        replicatePrepared(sequence_number, timing);
    }

    std::optional<std::string> PrimaryReplicator::get(const std::string &key) const
    {
        ensureReady();

        return database_.get(key);
    }

    bool PrimaryReplicator::erase(const std::string &key)
    {
        ensureReady();
        last_write_timing_.reset();

        auto start = Clock::now();
        auto sequence_number = database_.prepareDelete(key);

        if (!sequence_number)
        {
            return false;
        }

        ReplicationWriteTiming timing;
        timing.primary_prepare_ms = elapsedMilliseconds(start, Clock::now());

        replicatePrepared(*sequence_number, timing);

        return true;
    }

    void PrimaryReplicator::replicatePrepared(
        std::uint64_t sequence_number,
        ReplicationWriteTiming timing)
    {
        try
        {
            auto pending_operation = database_.pendingOperation();

            if (!pending_operation ||
                pending_operation->sequence_number != sequence_number)
            {
                throw std::runtime_error("Prepared operation is missing");
            }

            auto start = Clock::now();
            backup_client_.prepare(*pending_operation);
            timing.backup_prepare_ms = elapsedMilliseconds(start, Clock::now());

            start = Clock::now();
            database_.commitPrepared(sequence_number);
            timing.primary_commit_ms = elapsedMilliseconds(start, Clock::now());

            start = Clock::now();
            backup_client_.commit(sequence_number);
            timing.backup_commit_ms = elapsedMilliseconds(start, Clock::now());

            last_write_timing_ = timing;
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
        last_write_timing_.reset();

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

    std::optional<ReplicationWriteTiming> PrimaryReplicator::lastWriteTiming() const
    {
        return last_write_timing_;
    }

}