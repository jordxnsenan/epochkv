#pragma once

#include "epochkv/storage/LogRecord.hpp"
#include "epochkv/replication/BackupStatus.hpp"

#include <cstdint>
#include <string_view>

namespace epochkv
{

    class BackupClient
    {
    public:
        explicit BackupClient(std::uint16_t backup_port);

        void prepare(const LogRecord &operation) const;

        void commit(std::uint64_t sequence_number) const;

        BackupStatus status() const;

    private:
        std::uint16_t backup_port_;

        void sendAndCheck(const LogRecord &record, std::string_view expected_response) const;
    };

}
