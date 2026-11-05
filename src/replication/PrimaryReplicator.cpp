
#include "epochkv/replication/PrimaryReplicator.hpp"

#include <stdexcept>

namespace epochkv
{

    PrimaryReplicator::PrimaryReplicator(
        Database &database,
        const BackupClient &backup_client) : database_(database), backup_client_(backup_client)
    {
        // ensures existing database history is reconciled before replication resumes
        if (database_.lastCommittedSequence() > 0 ||
            database_.pendingOperation().has_value())
        {
            needs_reconciliation_ = true;
        }
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

}