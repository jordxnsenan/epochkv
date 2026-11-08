#pragma once

#include "epochkv/Database.hpp"
#include "epochkv/replication/BackupClient.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace epochkv
{

    class PrimaryReplicator
    {
    public:
        PrimaryReplicator(Database &database, const BackupClient &backup_client);

        void put(const std::string &key, const std::string &value);
        std::optional<std::string> get(const std::string &key) const;
        bool erase(const std::string &key);
        void reconcile();

    private:
        Database &database_;

        const BackupClient &backup_client_;

        bool needs_reconciliation_ = true;

        void replicatePrepared(std::uint64_t sequence_number);

        void ensureReady() const;
    };

}
