#pragma once

#include "epochkv/Database.hpp"
#include "epochkv/replication/BackupClient.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace epochkv
{

    struct ReplicationWriteTiming
    {
        double primary_prepare_ms = 0.0;
        double backup_prepare_ms = 0.0;
        double primary_commit_ms = 0.0;
        double backup_commit_ms = 0.0;
    };

    class PrimaryReplicator
    {
    public:
        PrimaryReplicator(Database &database, const BackupClient &backup_client);

        void put(const std::string &key, const std::string &value);
        std::optional<std::string> get(const std::string &key) const;
        bool erase(const std::string &key);
        void reconcile();

        std::optional<ReplicationWriteTiming> lastWriteTiming() const;

    private:
        Database &database_;

        const BackupClient &backup_client_;

        bool needs_reconciliation_ = true;

        void replicatePrepared(std::uint64_t sequence_number, ReplicationWriteTiming timing);

        void ensureReady() const;

        std::optional<ReplicationWriteTiming> last_write_timing_;
    };

}
