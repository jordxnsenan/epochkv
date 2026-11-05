#pragma once

#include "epochkv/storage/LogStore.hpp"
#include "epochkv/replication/BackupStatus.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace epochkv
{

    class BackupStore
    {
    public:
        explicit BackupStore(const std::filesystem::path &log_path);

        std::string processRecord(const LogRecord &record);

        bool requiresRestart() const;

        BackupStatus status() const;

        std::vector<LogRecord> history();

    private:
        LogStore log_store_;

        std::optional<LogRecord> pending_operation_;
        std::uint64_t last_committed_sequence_ = 0;

        bool requires_restart_ = false;

        void recover();
        void appendDurably(const LogRecord &record);
    };

}